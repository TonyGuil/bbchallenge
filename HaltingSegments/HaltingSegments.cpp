// HaltingSegments <param> <param>...
//   <param>: -N<states>            Machine states (5 or 6)
//            -D<database>          Seed database file (defaults to ../SeedDatabase.bin)
//            -V<verification file> Output file: verification data for decided machines
//            -I<input file>        Input file: list of machines to be analysed (default=all machines)
//            -U<undecided file>    Output file: remaining undecided machines
//            -X<test machine>      Machine to test
//            -M<machine spec>      Compact machine code (ASCII spec) to test
//            -L<machine limit>     Max no. of machines to test
//            -H<threads>           Number of threads to use
//            -O                    Print trace output
//            -W<width limit>       Max segment width (must be odd)
//            -S<stack depth>       Max stack depth (default 10000)
//
// The HaltingSegments Decider starts from the HALT state and recursively generates 
// all possible predecessor states within a given tape window, plus all possible
// predecessor states that could have left the window to the left or right before
// entering it again. If it can determine that none of the possible states is the
// starting state, then there is no way to reach the HALT state from the starting
// state, and the machine can be flagged as non-halting.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>
#include <set>
#include <thread>

#include "../TuringMachine.h"
#include "../Params.h"

// Number of machines to assign to each thread
#define DEFAULT_CHUNK_SIZE 256
static uint32_t ChunkSize = DEFAULT_CHUNK_SIZE ;

// Decider-specific Verification Data:
#define VERIF_INFO_LENGTH 20

// We need a special value to indicate that the contents of a cell on the tape
// are so far undetermined:
#define TAPE_ANY 3

// We have two different tape sentinels:
#define TAPE_SENTINEL_LEFT  4
#define TAPE_SENTINEL_RIGHT 5

//
// Command-line parameters
//

class CommandLineParams : public DeciderParams
  {
public:
  int WidthLimit ; bool WidthLimitPresent = false ;
  uint32_t MaxStackDepth = 10000 ;
  void Parse (int argc, char** argv) ;
  void PrintHelpAndExit [[noreturn]] (int status) ;
  } ;

static CommandLineParams Params ;

//
// HaltingSegment class
//

class HaltingSegment : public TuringMachineSpec
  {
public:
  HaltingSegment (uint32_t MachineStates, int WidthLimit)
  : TuringMachineSpec (MachineStates)
  , WidthLimit (WidthLimit)
    {
    WidthLimit |= 1 ; // Should be odd, but no harm in making sure

    // Allocate the tape workspace
    Tape = new uint8_t[WidthLimit + 2] ;
    Tape += (WidthLimit + 1) >> 1 ; // so Tape[0] is in the middle

    // Reserve maximum possible lengths for the predecessor vectors,
    // to avoid having to re-allocate them between searches (a mini-
    // optimisation)
    for (uint32_t i = 0 ; i <= MachineStates ; i++)
      TransitionTable[i].reserve (2 * MachineStates) ;

    // Statistics
    MaxDecidingDepth = new uint32_t[WidthLimit + 1] ;
    memset (MaxDecidingDepth, 0, (WidthLimit + 1) * sizeof (uint32_t)) ;
    MaxDecidingDepthMachine = new uint32_t[WidthLimit + 1] ;

    MinStat = INT_MAX ;
    MaxStat = INT_MIN ;
    }

  // Call RunDecider to analyse a single machine. MachineSpec is in the 30-byte
  // Seed Database format:
  bool RunDecider (const uint8_t* MachineSpec) ;

  uint8_t* Tape ;

  // Predecessor struct contains the parameters of a possible predecessor state
  struct Predecessor : public Transition
    {
    uint8_t State ;
    uint8_t Read ;
    } ;

  void ThreadFunction (int nMachines, const uint32_t* MachineIndexList,
    const uint8_t* MachineSpecList, uint8_t* VerificationEntryList) ;

  // Each state can be reached from a number of predecessor states:
  std::vector<Predecessor> TransitionTable[MAX_MACHINE_STATES + 1] ;

  // Possible previous configurations when leaving the segment, depending on tape contents
  std::vector<Predecessor> LeftOfSegment[2] ;
  std::vector<Predecessor> RightOfSegment[2] ;

  // The Configuration struct doesn't need to contain the tape contents,
  // because we update the tape dynamically as we recurse
  struct Configuration
    {
    uint8_t State ;
    int16_t TapeHead ;
    } ;

  // Call Recurse with Depth = 0 to start the search
  bool Recurse (uint32_t Depth, const Configuration& Config) ;

  // If we exit the halting segment to the left or right:
  bool ExitSegmentLeft (uint32_t Depth, uint8_t State) ;
  bool ExitSegmentRight (uint32_t Depth, uint8_t State) ;

  //
  // SEGMENT TREES
  //

  struct SimpleTree { SimpleTree* Next[2] ; } ;
  struct ForwardTree { ForwardTree* Next[2] ; } ;
  struct BackwardTree { BackwardTree* Next[2] ; } ;
  struct CompoundTree
    {
    CompoundTree* Next[2] ;
    ForwardTree* SubTree ;
    } ;

  size_t FindShorterOrEqual (const CompoundTree* Tree, const uint8_t* TapeHead) ;
  CompoundTree* Insert (CompoundTree* Tree, const uint8_t* TapeHead, size_t NodeIndex) ;

  size_t FindShorterOrEqual (const ForwardTree* subTree, const uint8_t* TapeHead) ;
  ForwardTree* Insert (ForwardTree* Tree, const uint8_t* TapeHead, size_t NodeIndex) ;

  size_t FindShorterOrEqual (const BackwardTree* subTree, const uint8_t* TapeHead) ;
  BackwardTree* Insert (BackwardTree* Tree, const uint8_t* TapeHead, size_t NodeIndex) ;

  template<class T> bool IsLeafNode (T* Tree)
    {
    return ((size_t)Tree & 1) != 0 ;
    }

  template<class T> uint32_t TreeAsLeafNode (T* Tree)
    {
    return (size_t)Tree >> 1 ;
    }

  template<class T> T* LeafNodeAsTree (size_t NodeIndex)
    {
    return (T*)(2 * NodeIndex + 1) ;
    }

  template<class TreeType> class TreePool
    {
    #define BLOCK_SIZE 100000
  public:
    TreePool()
      {
      FirstBlock = new Block ;
      CurrentBlock = FirstBlock ;
      TreeIndex = 0 ;
      }
    struct Block
      {
      Block* Next = 0 ;
      TreeType Tree[BLOCK_SIZE] ;
      } ;
    Block* FirstBlock ;
    Block* CurrentBlock ;
    uint32_t TreeIndex ;

    void Clear()
      {
      CurrentBlock = FirstBlock ;
      TreeIndex = 0 ;
      }

    TreeType* Allocate()
      {
      if (TreeIndex == BLOCK_SIZE)
        {
        if (CurrentBlock -> Next == 0) CurrentBlock -> Next = new Block ;
        CurrentBlock = CurrentBlock -> Next ;
        TreeIndex = 0 ;
        }
      return &CurrentBlock -> Tree[TreeIndex++] ;
      }
    } ;

  CompoundTree* AlreadySeen[MAX_MACHINE_STATES + 1][2] ;
  ForwardTree* ExitedLeft ;
  BackwardTree* ExitedRight ;

  TreePool<CompoundTree> CompoundTreePool ;
  TreePool<SimpleTree> SimpleTreePool ;

  uint32_t WidthLimit ; // Must be odd
  int HalfWidth ;  // Max absolute value of TapeHead = WidthLimit >> 1

  //
  // STATISTICS
  //

  int Leftmost, Rightmost ;
  uint32_t MaxDepth ;
  uint32_t nNodes ;
  uint32_t* MaxDecidingDepth ;
  uint32_t* MaxDecidingDepthMachine ;

  // Whatever we may want to know from time to time:
  int MaxStat ; uint32_t MaxStatMachine ;
  int MinStat ; uint32_t MinStatMachine ;
  } ;

int main (int argc, char** argv)
  {
  Params.Parse (argc, argv) ;

  Params.CheckParameters() ;
  Params.OpenFiles() ;

  TuringMachineReader Reader (&Params) ;

  // Write dummy dvf header
  Write32 (Params.fpVerify, 0) ;

  if (!Params.nThreadsPresent)
    {
    if (Reader.SingleEntry) Params.nThreads = 1 ;
    else
      {
      Params.nThreads = 4 ;
      char* env = getenv ("NUMBER_OF_PROCESSORS") ;
      if (env)
        {
        Params.nThreads = atoi (env) ;
        if (Params.nThreads == 0) Params.nThreads = 4 ;
        }
      printf ("nThreads = %d\n", Params.nThreads) ;
      }
    }
  std::vector<std::thread*> ThreadList (Params.nThreads) ;

  clock_t Timer = clock() ;

  HaltingSegment** DeciderArray = new HaltingSegment*[Params.nThreads] ;
  uint32_t** MachineIndexList = new uint32_t*[Params.nThreads] ;
  uint8_t** MachineSpecList = new uint8_t*[Params.nThreads] ;
  uint8_t** VerificationEntryList = new uint8_t*[Params.nThreads] ;
  uint32_t* ChunkSizeArray = new uint32_t[Params.nThreads] ;
  for (uint32_t i = 0 ; i < Params.nThreads ; i++)
    {
    DeciderArray[i] = new HaltingSegment (Params.MachineStates, Params.WidthLimit) ;
    MachineIndexList[i] = new uint32_t[ChunkSize] ;
    MachineSpecList[i] = new uint8_t[Reader.MachineSpecSize * ChunkSize] ;
    VerificationEntryList[i] = new uint8_t[VERIF_ENTRY_LENGTH * ChunkSize] ;
    }

  uint32_t nDecided = 0 ;
  uint32_t nTimeLimitedDecided = 0 ;
  uint32_t nSpaceLimitedDecided = 0 ;
  uint32_t nCompleted = 0 ;
  int LastPercent = -1 ;

  while (nCompleted < Reader.nMachines)
    {
    uint32_t nRemaining = Reader.nMachines - nCompleted ;
    if (nRemaining >= Params.nThreads * ChunkSize)
      {
      for (uint32_t i = 0 ; i < Params.nThreads ; i++) ChunkSizeArray[i] = ChunkSize ;
      }
    else
      {
      for (uint32_t i = 0 ; i < Params.nThreads ; i++)
        {
        ChunkSizeArray[i] = nRemaining / (Params.nThreads - i) ;
        nRemaining -= ChunkSizeArray[i] ;
        }
      }

    for (uint32_t i = 0 ; i < Params.nThreads ; i++)
      {
      for (uint32_t j = 0 ; j < ChunkSizeArray[i] ; j++)
        MachineIndexList[i][j] = Reader.Next (MachineSpecList[i] + j * Reader.MachineSpecSize) ;

      ThreadList[i] = new std::thread (&HaltingSegment::ThreadFunction, DeciderArray[i],
        ChunkSizeArray[i], MachineIndexList[i], MachineSpecList[i], VerificationEntryList[i]) ;
      }

    for (uint32_t i = 0 ; i < Params.nThreads ; i++)
      {
      // Wait for thread i to finish
      ThreadList[i] -> join() ;
      delete ThreadList[i] ;

      const uint8_t* MachineSpec = MachineSpecList[i] ;
      const uint8_t* VerificationEntry = VerificationEntryList[i] ;
      for (uint32_t j = 0 ; j < ChunkSizeArray[i] ; j++)
        {
        if (Load32 (VerificationEntry + 4))
          {
          if (Params.fpVerify && fwrite (VerificationEntry, VERIF_ENTRY_LENGTH, 1, Params.fpVerify) != 1)
            printf ("Error writing file\n"), exit (1) ;
          nDecided++ ;
          if (MachineIndexList[i][j] < Reader.nTimeLimited) nTimeLimitedDecided++ ;
          else nSpaceLimitedDecided++ ;
          }
        else Write32 (Params.fpUndecided, MachineIndexList[i][j]) ;
        MachineSpec += Reader.MachineSpecSize ;
        VerificationEntry += VERIF_ENTRY_LENGTH ;
        nCompleted++ ;
        }
      }

    int Percent = (nCompleted * 100LL) / Reader.nMachines ;
    if (Percent != LastPercent)
      {
      LastPercent = Percent ;
      printf ("\r%d%% %d %d", Percent, nCompleted, nDecided) ;
      fflush (stdout) ;
      }
    }
  printf ("\n") ;

  if (Params.fpUndecided) fclose (Params.fpUndecided) ;
  fclose (Params.fpInput) ;

  if (Params.fpVerify)
    {
    // Write the verification file header
    if (fseek (Params.fpVerify, 0 , SEEK_SET))
      printf ("\nfseek failed\n"), exit (1) ;
    Write32 (Params.fpVerify, nDecided) ;
    fclose (Params.fpVerify) ;
    }

  Timer = clock() - Timer ;

  printf ("\nDecided %d out of %d\n", nDecided, Reader.nMachines) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;

  printf ("\nMax search depth for decided machines by segment width:\n") ;
  for (int HalfWidth = 1 ; 2 * HalfWidth + 1 <= Params.WidthLimit ; HalfWidth++)
    {
    uint32_t Max = 0 ;
    uint32_t MaxMachineIndex ;
    for (uint32_t i = 0 ; i < Params.nThreads ; i++)
      if (DeciderArray[i] -> MaxDecidingDepth[HalfWidth] > Max)
        {
        Max = DeciderArray[i] -> MaxDecidingDepth[HalfWidth] ;
        MaxMachineIndex = DeciderArray[i] -> MaxDecidingDepthMachine[HalfWidth] ;
        }
    if (Max) printf ("%d: %d (#%d)\n", 2 * HalfWidth + 1, Max, MaxMachineIndex) ;
    }

  int MinStat = INT_MAX ;
  uint32_t MinStatMachine = 0 ;
  int MaxStat = INT_MIN ;
  uint32_t MaxStatMachine = 0 ;
  for (uint32_t i = 0 ; i < Params.nThreads ; i++)
    {
    if (DeciderArray[i] -> MaxStat > MaxStat)
      {
      MaxStat = DeciderArray[i] -> MaxStat ;
      MaxStatMachine = DeciderArray[i] -> MaxStatMachine ;
      }
    if (DeciderArray[i] -> MinStat < MinStat)
      {
      MinStat = DeciderArray[i] -> MinStat ;
      MinStatMachine = DeciderArray[i] -> MinStatMachine ;
      }
    }
  if (MinStat != INT_MAX) printf ("\n%d: MinStat = %d\n", MinStatMachine, MinStat) ;
  if (MaxStat != INT_MIN) printf ("\n%d: MaxStat = %d\n", MaxStatMachine, MaxStat) ;
  }

void HaltingSegment::ThreadFunction (int nMachines, const uint32_t* MachineIndexList,
  const uint8_t* MachineSpecList, uint8_t* VerificationEntryList)
  {
  while (nMachines--)
    {
    SeedDatabaseIndex = *MachineIndexList++ ;
    if (RunDecider (MachineSpecList))
      {
      Save32 (VerificationEntryList, SeedDatabaseIndex) ;
      Save32 (VerificationEntryList + 4, uint32_t (DeciderTag::HALTING_SEGMENT)) ;
      Save32 (VerificationEntryList + 8, VERIF_INFO_LENGTH) ;
      Save32 (VerificationEntryList + 12, Leftmost) ;
      Save32 (VerificationEntryList + 16, Rightmost) ;
      Save32 (VerificationEntryList + 20, MaxDepth) ;
      Save32 (VerificationEntryList + 24, nNodes) ;
      Save32 (VerificationEntryList + 28, 2 * HalfWidth + 1) ;
      }
    else Save32 (VerificationEntryList + 4, uint32_t (DeciderTag::NONE)) ;

    MachineSpecList += MachineSpecSize ;
    VerificationEntryList += VERIF_ENTRY_LENGTH ;
    }
  }

bool HaltingSegment::RunDecider (const uint8_t* MachineSpec)
  {
  for (uint32_t i = 0 ; i <= MachineStates ; i++) TransitionTable[i].clear() ;

  for (int i = 0 ; i <= 1 ; i++)
    {
    LeftOfSegment[i].clear() ;
    RightOfSegment[i].clear() ;
    }

  // Build the backward transition table from the MachineSpec
  for (uint8_t State = 1 ; State <= MachineStates ; State++)
    {
    for (uint8_t Cell = 0 ; Cell <= 1 ; Cell++)
      {
      Predecessor T ;
      UnpackSpec (&T, MachineSpec) ;
      MachineSpec += 3 ;
      T.State = State ;
      T.Read = Cell ;
      TransitionTable[T.Next].push_back (T) ;

      if (T.Next != 0)
        {
        if (T.Move) LeftOfSegment[T.Write].push_back (T) ;
        else RightOfSegment[T.Write].push_back (T) ;
        }
      }
    }

  for (HalfWidth = 1 ; 2 * HalfWidth + 1 <= Params.WidthLimit ; HalfWidth++)
    {
    // Start in state 0 with unspecified tape
    memset (Tape - HalfWidth, TAPE_ANY, 2 * HalfWidth + 1) ;
    Tape[-HalfWidth - 1] = TAPE_SENTINEL_LEFT ;
    Tape[HalfWidth + 1] = TAPE_SENTINEL_RIGHT ;
    Configuration StartConfig ;
    StartConfig.State = 0 ;
    StartConfig.TapeHead = 0 ;

    CompoundTreePool.Clear() ;
    SimpleTreePool.Clear() ;
    memset (AlreadySeen, 0, sizeof (AlreadySeen)) ;
    ExitedLeft = 0 ;
    ExitedRight = 0 ;

    MaxDepth = nNodes = 0 ;
    Leftmost = Rightmost = 0 ;

    if (Recurse (0, StartConfig))
      {
      if (MaxDepth > MaxDecidingDepth[HalfWidth])
        {
        MaxDecidingDepth[HalfWidth] = MaxDepth ;
        MaxDecidingDepthMachine[HalfWidth] = SeedDatabaseIndex ;
        }
      return true ;
      }
    }

  return false ;
  }

bool HaltingSegment::Recurse (uint32_t Depth, const Configuration& Config)
  {
  // Check for possible match with starting configuration
  if (Config.State == 1)
    {
    int i ; for (i = -HalfWidth ; i <= int(HalfWidth) ; i++)
      if (Tape[i] != 0 && Tape[i] != TAPE_ANY) break ;
    if (i > (int)HalfWidth)
      return false ;
    }

  if (Depth != 0)
    {
    nNodes++ ;
    if (Params.TraceOutput)
      {
      printf ("State: %c ; ", Config.State + '@') ;
      for (int i = -HalfWidth - 1 ; i <= (int)HalfWidth + 1 ; i++)
        {
        printf (i == Config.TapeHead ? "[" : i == Config.TapeHead + 1 ? "]" : " ") ;
        printf ("%c", "01*.__"[Tape[i]]) ;
        }
      printf (Config.TapeHead == (int)HalfWidth + 1 ? "]" : " ") ;
      printf (" ; Node: %d ; Depth: %d\n", nNodes, Depth) ;
      }
    }

  if (++Depth > MaxDepth) 
   {
   if (Depth > Params.MaxStackDepth) return false ;
   MaxDepth = Depth ;
   }

  // If we've seen this already, return true
  if (Tape[Config.TapeHead] <= 1)
    {
    CompoundTree*& Tree = AlreadySeen[Config.State][Tape[Config.TapeHead]] ;
    if (FindShorterOrEqual (Tree, Tape + Config.TapeHead)) return true ;
    Tree = Insert (Tree, Tape + Config.TapeHead, nNodes) ;
    }

  Configuration PrevConfig ;

  // Go through the transitions in reverse order, to match Iijil's Go implementation
  bool ExitedLeft = false, ExitedRight = false ;
  for (int i = TransitionTable[Config.State].size() - 1 ; i >= 0 ; i--)
    {
    const auto& T = TransitionTable[Config.State][i] ;

    // Update the tape head
    if (Depth == 1) PrevConfig.TapeHead = Config.TapeHead ;
    else if (T.Move)
      {
      PrevConfig.TapeHead = Config.TapeHead + 1 ;
      if (PrevConfig.TapeHead > Rightmost) Rightmost = PrevConfig.TapeHead ;
      }
    else
      {
      PrevConfig.TapeHead = Config.TapeHead - 1 ;
      if (PrevConfig.TapeHead < Leftmost) Leftmost = PrevConfig.TapeHead ;
      }

    uint8_t Cell = Tape[PrevConfig.TapeHead] ;
    switch (Cell)
      {
      case TAPE_SENTINEL_LEFT: // Exiting tape segment to the left
        if (!ExitedLeft)
          {
          if (!ExitSegmentLeft (Depth, Config.State)) return false ;
          ExitedLeft = true ;
          }
        continue ;

      case TAPE_SENTINEL_RIGHT: // Exiting tape segment to the right
        if (!ExitedRight)
          {
          if (!ExitSegmentRight (Depth, Config.State)) return false ;
          ExitedRight = true ;
          }
        continue ;

      case TAPE_ANY: // New tape cell reached, so just write the expected value
        Tape[PrevConfig.TapeHead] = T.Read ;
        break ;

      default:
        if (Tape[PrevConfig.TapeHead] != T.Write)
          {
          // Clash with required tape cell value, so this is an impossible path
          Tape[PrevConfig.TapeHead] = Cell ; // Restore the previous value
          continue ;
          }

        // Update the tape with the value that it had to contain to reach this state
        Tape[PrevConfig.TapeHead] = T.Read ;
        break ;
      }

    // Perform a backwards step and search deeper
    PrevConfig.State = T.State ;

    if (!Recurse (Depth, PrevConfig)) return false ;

    Tape[PrevConfig.TapeHead] = Cell ;
    }

  // No search returned false, i.e. all searches terminated at a finite depth.
  // So we can't reach this state from the starting position:
  return true ;
  }

bool HaltingSegment::ExitSegmentLeft (uint32_t Depth, uint8_t State)
  {
  // Check for all zeroes or unset
  int i ; for (i = -HalfWidth ; i <= int(HalfWidth) ; i++)
    if (Tape[i] != 0 && Tape[i] != TAPE_ANY) break ;
  if (i > (int)HalfWidth)
    return false ;

  // If we've seen this already, return true
  if (FindShorterOrEqual (ExitedLeft, Tape - HalfWidth)) return true ;

  nNodes++ ;

  if (Params.TraceOutput)
    {
    printf ("State: * ; [_]") ;
    for (int i = -HalfWidth ; i <= (int)HalfWidth ; i++)
      printf ("%c ", "01*.__"[Tape[i]]) ;
    printf ("_  ; Node: %d ; Depth: %d\n", nNodes, Depth) ;
    }

  if (++Depth > MaxDepth) MaxDepth = Depth ;

  ExitedLeft = Insert (ExitedLeft, Tape - HalfWidth, nNodes) ;

  Configuration PrevConfig ;
  PrevConfig.TapeHead = -HalfWidth ;
  uint8_t Cell = Tape[-HalfWidth] ;

  // Go through the transitions in reverse order, to match Iijil's Go implementation
  for (int i = LeftOfSegment[Cell].size() - 1 ; i >= 0 ; i--)
    {
    const auto& T = LeftOfSegment[Cell][i] ;
    PrevConfig.State = T.State ;
    Tape[-HalfWidth] = T.Read ;
    if (!Recurse (Depth, PrevConfig)) return false ;
    Tape[-HalfWidth] = Cell ;
    }

  return true ;
  }

bool HaltingSegment::ExitSegmentRight (uint32_t Depth, uint8_t State)
  {
  // Check for all zeroes or unset
  int i ; for (i = -HalfWidth ; i <= int(HalfWidth) ; i++)
    if (Tape[i] != 0 && Tape[i] != TAPE_ANY) break ;
  if (i > (int)HalfWidth)
    return false ;

  // If we've seen this already, return true
  if (FindShorterOrEqual (ExitedRight, Tape + HalfWidth)) return true ;

  nNodes++ ;

  if (Params.TraceOutput)
    {
    printf ("State: * ;  _") ;
    for (int i = -HalfWidth ; i <= (int)HalfWidth ; i++)
      printf (" %c", "01*.__"[Tape[i]]) ;
    printf ("[_] ; Node: %d ; Depth: %d\n", nNodes, Depth) ;
    }

  if (++Depth > MaxDepth) MaxDepth = Depth ;

  ExitedRight = Insert (ExitedRight, Tape + HalfWidth, nNodes) ;

  Configuration PrevConfig ;
  PrevConfig.TapeHead = HalfWidth ;
  uint8_t Cell = Tape[HalfWidth] ;

  // Go through the transitions in reverse order, to match Iijil's Go implementation
  for (int i = RightOfSegment[Cell].size() - 1 ; i >= 0 ; i--)
    {
    const auto& T = RightOfSegment[Cell][i] ;
    PrevConfig.State = T.State ;
    Tape[HalfWidth] = T.Read ;
    if (!Recurse (Depth, PrevConfig)) return false ;
    Tape[HalfWidth] = Cell ;
    }

  return true ;
  }

size_t HaltingSegment::FindShorterOrEqual (const CompoundTree* Tree, const uint8_t* TapeHead)
  {
  if (Tree == nullptr) return 0 ;
  for (const uint8_t* p = TapeHead - 1 ; Tree ; p--)
    {
    size_t NodeIndex = FindShorterOrEqual (Tree -> SubTree, TapeHead + 1) ;
    if (NodeIndex) return NodeIndex ;
    if (*p > 1) return 0 ;
    Tree = Tree -> Next[*p] ;
    }
  return 0 ;
  }

HaltingSegment::CompoundTree* HaltingSegment::Insert (CompoundTree* Tree, const uint8_t* TapeHead, size_t NodeIndex)
  {
  if (Tree == 0)
    {
    Tree = CompoundTreePool.Allocate() ;
    Tree -> Next[0] = Tree -> Next[1] = 0 ;
    Tree -> SubTree = 0 ;
    }
  CompoundTree* TreeNode = Tree ;
  for (const uint8_t* p = TapeHead - 1 ; *p <= 1 ; p--)
    {
    if (TreeNode -> Next[*p] == 0)
      {
      TreeNode -> Next[*p] = CompoundTreePool.Allocate() ;
      TreeNode -> Next[*p] -> Next[0] = TreeNode -> Next[*p] -> Next[1] = 0 ;
      TreeNode -> Next[*p] -> SubTree = 0 ;
      }
    TreeNode = TreeNode -> Next[*p] ;
    }
  TreeNode -> SubTree = Insert (TreeNode -> SubTree, TapeHead + 1, NodeIndex) ;
  return Tree ;
  }

size_t HaltingSegment::FindShorterOrEqual (const ForwardTree* Tree, const uint8_t* TapeHead)
  {
  // Tree = 0 means no entries here:
  if (Tree == 0) return 0 ;
  if (IsLeafNode (Tree)) return TreeAsLeafNode (Tree) ;

  for ( ; ; TapeHead++)
    {
    if (*TapeHead > 1) return 0 ;
    Tree = Tree -> Next[*TapeHead] ;
    if (Tree == 0) return 0 ;
    if (IsLeafNode (Tree)) return TreeAsLeafNode (Tree) ;
    if (Tree -> Next[0] == 0 && Tree -> Next[1] == 0)
      printf ("Error 2 in FindShorterOrEqual (ForwardTree)\n"), exit (1) ;
    }
  }

HaltingSegment::ForwardTree* HaltingSegment::Insert (ForwardTree* Tree, const uint8_t* TapeHead, size_t NodeIndex)
  {
  if (*TapeHead > 1) return LeafNodeAsTree<ForwardTree> (NodeIndex) ; // Empty string

  if (Tree == 0)
    {
    Tree = (ForwardTree*)SimpleTreePool.Allocate() ;
    Tree -> Next[0] = Tree -> Next[1] = 0 ;
    }
  else if (Tree -> Next[0] == 0 && Tree -> Next[1] == 0)
    printf ("Error 2 in Insert (ForwardTree)\n"), exit (1) ;

  ForwardTree* TreeNode = Tree ;
  for ( ; ; )
    {
    if (TapeHead[1] > 1)
      {
      TreeNode -> Next[*TapeHead] = LeafNodeAsTree<ForwardTree> (NodeIndex) ;
      return Tree ;
      }
    if (TreeNode -> Next[*TapeHead] == 0)
      {
      TreeNode -> Next[*TapeHead] = (ForwardTree*)SimpleTreePool.Allocate() ;
      TreeNode -> Next[*TapeHead] -> Next[0] = TreeNode -> Next[*TapeHead] -> Next[1] = 0 ;
      }
    else if (IsLeafNode (TreeNode -> Next[*TapeHead]))
      printf ("Error 1 in Insert (ForwardTree)\n"), exit (1) ;

    TreeNode = TreeNode -> Next[*TapeHead++] ;
    }
  }

size_t HaltingSegment::FindShorterOrEqual (const BackwardTree* Tree, const uint8_t* TapeHead)
  {
  // Tree = 0 means no entries here:
  if (Tree == 0) return 0 ;
  if (IsLeafNode (Tree)) return TreeAsLeafNode (Tree) ;

  for ( ; ; TapeHead--)
    {
    if (*TapeHead > 1) return 0 ;
    Tree = Tree -> Next[*TapeHead] ;
    if (Tree == 0) return 0 ;
    if (IsLeafNode (Tree)) return TreeAsLeafNode (Tree) ;
    if (Tree -> Next[0] == 0 && Tree -> Next[1] == 0)
      printf ("Error 2 in FindShorterOrEqual (BackwardTree)\n"), exit (1) ;
    }
  }

HaltingSegment::BackwardTree* HaltingSegment::Insert (BackwardTree* Tree, const uint8_t* TapeHead, size_t NodeIndex)
  {
  if (*TapeHead > 1) return LeafNodeAsTree<BackwardTree> (NodeIndex) ; // Empty string

  if (Tree == 0)
    {
    Tree = (BackwardTree*)SimpleTreePool.Allocate() ;
    Tree -> Next[0] = Tree -> Next[1] = 0 ;
    }
  else if (Tree -> Next[0] == 0 && Tree -> Next[1] == 0)
    printf ("Error 2 in Insert (BackwardTree)\n"), exit (1) ;

  BackwardTree* TreeNode = Tree ;
  for ( ; ; )
    {
    if (TapeHead[-1] > 1)
      {
      TreeNode -> Next[*TapeHead] = LeafNodeAsTree<BackwardTree> (NodeIndex) ;
      return Tree ;
      }
    if (TreeNode -> Next[*TapeHead] == 0)
      {
      TreeNode -> Next[*TapeHead] = (BackwardTree*)SimpleTreePool.Allocate() ;
      TreeNode -> Next[*TapeHead] -> Next[0] = TreeNode -> Next[*TapeHead] -> Next[1] = 0 ;
      }
    else if (IsLeafNode (TreeNode -> Next[*TapeHead]))
      printf ("Error 1 in Insert (BackwardTree)\n"), exit (1) ;

    TreeNode = TreeNode -> Next[*TapeHead--] ;
    }
  }

void CommandLineParams::Parse (int argc, char** argv)
  {
  if (argc == 1) PrintHelpAndExit (0) ;

  for (argc--, argv++ ; argc ; argc--, argv++)
    {
    if (DeciderParams::ParseParam (argv[0])) continue ;
    if (argv[0][0] != '-') printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
    switch (toupper (argv[0][1]))
      {
      case 'W':
        WidthLimit = atoi (&argv[0][2]) ;
        if (!(WidthLimit & 1)) printf ("Segment width limit must be odd\n"), exit (1) ;
        WidthLimitPresent = true ;
        break ;

      case 'S':
        MaxStackDepth = atoi (&argv[0][2]) ;
        break ;

      default:
        printf ("Invalid parameter \"%s\"\n", argv[0]) ;
        PrintHelpAndExit (1) ;
      }
    }

  if (!WidthLimitPresent) printf ("Width limit not specified\n"), PrintHelpAndExit (1) ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf ("HaltingSegments <param> <param>...") ;
  DeciderParams::PrintHelp() ;
  printf (R"*RAW*(
           -W<width limit>       Max segment width (must be odd)
           -S<stack depth>       Max stack depth
)*RAW*") ;
  exit (status) ;
  }
