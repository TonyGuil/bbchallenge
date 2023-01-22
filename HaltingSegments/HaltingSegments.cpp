// HaltingSegments <param> <param>...
//
//   <param>: -S<seed database>      Seed database file (defaults to ../SeedDatabase.bin)
//            -I<input file>         Input file: list of machines to be analysed
//            -V<verification file>  Output file: verification data for decided machines
//            -U<undecided file>     Output file: remaining undecided machines
//            -W<width limit>        Max absolute value of tape head
//            -M<threads>            Number of threads to run
//            -X<test machine>       Machine to test
//            -T                     Print trace output
//            -L<machine limit>      Max no. of machines to test
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
#include <boost/thread.hpp>

#include "../bbchallenge.h"

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

class CommandLineParams
  {
public:
  static std::string SeedDatabaseFile ;
  static std::string InputFile ;
  static std::string VerificationFile ;
  static std::string UndecidedFile ;
  static int WidthLimit ;         static bool WidthLimitPresent ;
  static uint32_t nThreads ;      static bool nThreadsPresent ;
  static uint32_t TestMachine ;   static bool TestMachinePresent ;
  static uint32_t MachineLimit ;  static bool MachineLimitPresent ;
  static bool TraceOutput ;
  static void Parse (int argc, char** argv) ;
  static void PrintHelpAndExit [[noreturn]] (int status) ;
  } ;

std::string CommandLineParams::SeedDatabaseFile ;
std::string CommandLineParams::InputFile ;
std::string CommandLineParams::VerificationFile ;
std::string CommandLineParams::UndecidedFile ;
int CommandLineParams::WidthLimit ;         bool CommandLineParams::WidthLimitPresent ;
uint32_t CommandLineParams::nThreads ;      bool CommandLineParams::nThreadsPresent ;
uint32_t CommandLineParams::TestMachine ;   bool CommandLineParams::TestMachinePresent ;
bool CommandLineParams::TraceOutput ;
uint32_t CommandLineParams::MachineLimit ;  bool CommandLineParams::MachineLimitPresent ;

//
// HaltingSegment class
//

class HaltingSegment
  {
public:
  HaltingSegment (int WidthLimit) : WidthLimit (WidthLimit)
    {
    WidthLimit |= 1 ; // Should be odd, but no harm in making sure

    // Allocate the tape workspace
    Tape = new uint8_t[WidthLimit + 2] ;
    Tape += (WidthLimit + 1) >> 1 ; // so Tape[0] is in the middle

    // Reserve maximum possible lengths for the backward transition vectors,
    // to avoid having to re-allocate them between searches (a mini-
    // optimisation)
    for (int i = 0 ; i <= NSTATES ; i++)
      TransitionTable[i].reserve (2 * NSTATES) ;

    // Statistics
    MaxDecidingDepth = new uint32_t[WidthLimit + 1] ;
    memset (MaxDecidingDepth, 0, (WidthLimit + 1) * sizeof (uint32_t)) ;
    MaxDecidingDepthMachine = new uint32_t[WidthLimit + 1] ;
    TotalMatches = 0 ;

    MinStat = INT_MAX ;
    MaxStat = INT_MIN ;
    }

  // Call RunDecider to analyse a single machine. MachineSpec is in the 30-byte
  // Seed Database format:
  bool RunDecider (const uint8_t* MachineSpec) ;

  uint32_t SeedDatabaseIndex ;
  uint8_t* Tape ;

  // Transition struct contains the parameters of a possible predecessor state
  struct Transition
    {
    uint8_t State ;
    uint8_t Read ;
    uint8_t Write ;
    uint8_t Move ;
    } ;

  void ThreadFunction (int nMachines, const uint32_t* MachineIndexList,
    const uint8_t* MachineSpecList, uint8_t* VerificationEntryList) ;

  // Each state can be reached from a number of predecessor states:
  std::vector<Transition> TransitionTable[NSTATES + 1] ;

  // Possible previous configurations when leaving the segment, depending on tape contents
  std::vector<Transition> LeftOfSegment[2] ;
  std::vector<Transition> RightOfSegment[2] ;

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

  uint32_t FindShorterOrEqual (const CompoundTree* Tree, const uint8_t* TapeHead) ;
  CompoundTree* Insert (CompoundTree* Tree, const uint8_t* TapeHead, uint32_t NodeIndex) ;

  uint32_t FindShorterOrEqual (const ForwardTree* subTree, const uint8_t* TapeHead) ;
  ForwardTree* Insert (ForwardTree* Tree, const uint8_t* TapeHead, uint32_t NodeIndex) ;

  uint32_t FindShorterOrEqual (const BackwardTree* subTree, const uint8_t* TapeHead) ;
  BackwardTree* Insert (BackwardTree* Tree, const uint8_t* TapeHead, uint32_t NodeIndex) ;

  template<class T> bool IsLeafNode (T* Tree)
    {
    return ((uint32_t)Tree & 1) != 0 ;
    }

  template<class T> uint32_t TreeAsLeafNode (T* Tree)
    {
    return (uint32_t)Tree >> 1 ;
    }

  template<class T> T* LeafNodeAsTree (uint32_t NodeIndex)
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

  CompoundTree* AlreadySeen[6][2] ;
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
  uint32_t nMatches ;
  uint32_t TotalMatches ;

  // Whatever we may want to know from time to time:
  int MaxStat ; uint32_t MaxStatMachine ;
  int MinStat ; uint32_t MinStatMachine ;
  } ;

int main (int argc, char** argv)
  {
  CommandLineParams::Parse (argc, argv) ;

  TuringMachineReader Reader (CommandLineParams::SeedDatabaseFile.c_str()) ;

  uint8_t MachineSpec[MACHINE_SPEC_SIZE] ;
  if (CommandLineParams::TestMachinePresent)
    {
    HaltingSegment Decider (CommandLineParams::WidthLimit) ;
    Decider.SeedDatabaseIndex = CommandLineParams::TestMachine ;
    Reader.Read (Decider.SeedDatabaseIndex, MachineSpec) ;
    printf ("%d\n", Decider.RunDecider (MachineSpec)) ;
    printf ("%d %d\n", Decider.MaxDepth, Decider.nNodes) ;
    exit (0) ;
    }

  // fpin contains the list of machines to analyse
  FILE* fpin = fopen (CommandLineParams::InputFile.c_str(), "rb") ;
  if (fpin == NULL) printf ("Can't open input file\n"), exit (1) ;
  if (fseek (fpin, 0, SEEK_END))
    printf ("fseek failed\n"), exit (1) ;
  uint32_t InputFileSize = ftell (fpin) ;
  if (InputFileSize & 3) // Must be a multiple of 4 bytes
    printf ("Invalid input file %s\n", CommandLineParams::InputFile.c_str()), exit (1) ;
  if (fseek (fpin, 0, SEEK_SET))
    printf ("fseek failed\n"), exit (1) ;

  if (!CommandLineParams::nThreadsPresent)
    {
    CommandLineParams::nThreads = 4 ;
    char* env = getenv ("NUMBER_OF_PROCESSORS") ;
    if (env)
      {
      CommandLineParams::nThreads = atoi (env) ;
      if (CommandLineParams::nThreads == 0) CommandLineParams::nThreads = 4 ;
      }
    printf ("nThreads = %d\n", CommandLineParams::nThreads) ;
    }
  std::vector<boost::thread*> ThreadList (CommandLineParams::nThreads) ;

  uint32_t nMachines = InputFileSize >> 2 ;
  if (CommandLineParams::MachineLimitPresent) nMachines = CommandLineParams::MachineLimit ;

  // Make sure the progress indicator updates reasonably often
  if (CommandLineParams::nThreads * ChunkSize * 50 > nMachines)
    ChunkSize = 1 + nMachines / (50 * CommandLineParams::nThreads) ;

  FILE* fpUndecided = 0 ;
  if (!CommandLineParams::UndecidedFile.empty())
    {
    fpUndecided = fopen (CommandLineParams::UndecidedFile.c_str(), "wb") ;
    if (fpUndecided == NULL)
      printf ("Can't open output file \"%s\"\n", CommandLineParams::UndecidedFile.c_str()), exit (1) ;
    }

  FILE* fpVerif = 0 ;
  if (!CommandLineParams::VerificationFile.empty())
    {
    fpVerif = fopen (CommandLineParams::VerificationFile.c_str(), "wb") ;
    if (fpVerif == NULL)
      printf ("Can't open output file \"%s\"\n", CommandLineParams::VerificationFile.c_str()), exit (1) ;
    }

  // Write dummy dvf header
  Write32 (fpVerif, 0) ;

  clock_t Timer = clock() ;

  HaltingSegment** DeciderArray = new HaltingSegment*[CommandLineParams::nThreads] ;
  uint32_t** MachineIndexList = new uint32_t*[CommandLineParams::nThreads] ;
  uint8_t** MachineSpecList = new uint8_t*[CommandLineParams::nThreads] ;
  uint8_t** VerificationEntryList = new uint8_t*[CommandLineParams::nThreads] ;
  uint32_t* ChunkSizeArray = new uint32_t[CommandLineParams::nThreads] ;
  for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
    {
    DeciderArray[i] = new HaltingSegment (CommandLineParams::WidthLimit) ;
    MachineIndexList[i] = new uint32_t[ChunkSize] ;
    MachineSpecList[i] = new uint8_t[MACHINE_SPEC_SIZE * ChunkSize] ;
    VerificationEntryList[i] = new uint8_t[VERIF_ENTRY_LENGTH * ChunkSize] ;
    }

  uint32_t nDecided = 0 ;
  uint32_t nTimeLimitedDecided = 0 ;
  uint32_t nSpaceLimitedDecided = 0 ;
  uint32_t nCompleted = 0 ;
  int LastPercent = -1 ;

  // Default thread stack size is 2 megabytes, but we need more:
  boost::thread::attributes ThreadAttributes ;
  ThreadAttributes.set_stack_size (0x2000000) ; // 32 megabytes

  while (nCompleted < nMachines)
    {
    uint32_t nRemaining = nMachines - nCompleted ;
    if (nRemaining >= CommandLineParams::nThreads * ChunkSize)
      {
      for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++) ChunkSizeArray[i] = ChunkSize ;
      }
    else
      {
      for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
        {
        ChunkSizeArray[i] = nRemaining / (CommandLineParams::nThreads - i) ;
        nRemaining -= ChunkSizeArray[i] ;
        }
      }

    std::vector<boost::thread*> ThreadList (CommandLineParams::nThreads) ;
    for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
      {
      for (uint32_t j = 0 ; j < ChunkSizeArray[i] ; j++)
        {
        MachineIndexList[i][j] = Read32 (fpin) ;
        Reader.Read (MachineIndexList[i][j], MachineSpecList[i] + j * MACHINE_SPEC_SIZE) ;
        }

      ThreadList[i] = new boost::thread (ThreadAttributes, boost::bind (
        &HaltingSegment::ThreadFunction, DeciderArray[i], ChunkSizeArray[i],
        MachineIndexList[i], MachineSpecList[i], VerificationEntryList[i])) ;
      }

    for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
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
          if (fpVerif && fwrite (VerificationEntry, VERIF_ENTRY_LENGTH, 1, fpVerif) != 1)
            printf ("Error writing file\n"), exit (1) ;
          nDecided++ ;
          if (MachineIndexList[i][j] < Reader.nTimeLimited) nTimeLimitedDecided++ ;
          else nSpaceLimitedDecided++ ;
          }
        else Write32 (fpUndecided, MachineIndexList[i][j]) ;
        MachineSpec += MACHINE_SPEC_SIZE ;
        VerificationEntry += VERIF_ENTRY_LENGTH ;
        nCompleted++ ;
        }
      }

    int Percent = (nCompleted * 100LL) / nMachines ;
    if (Percent != LastPercent)
      {
      LastPercent = Percent ;
      printf ("\r%d%% %d %d", Percent, nCompleted, nDecided) ;
      }
    }
  printf ("\n") ;

  if (fpUndecided) fclose (fpUndecided) ;
  fclose (fpin) ;

  if (fpVerif)
    {
    // Write the verification file header
    if (fseek (fpVerif, 0 , SEEK_SET))
      printf ("\nfseek failed\n"), exit (1) ;
    Write32 (fpVerif, nDecided) ;
    fclose (fpVerif) ;
    }

  Timer = clock() - Timer ;

  printf ("\nDecided %d out of %d\n", nDecided, nMachines) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;

  printf ("\nMax search depth for decided machines by segment width:\n") ;
  for (int HalfWidth = 1 ; 2 * HalfWidth + 1 <= CommandLineParams::WidthLimit ; HalfWidth++)
    {
    uint32_t Max = 0 ;
    uint32_t MaxMachineIndex ;
    for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
      if (DeciderArray[i] -> MaxDecidingDepth[HalfWidth] > Max)
        {
        Max = DeciderArray[i] -> MaxDecidingDepth[HalfWidth] ;
        MaxMachineIndex = DeciderArray[i] -> MaxDecidingDepthMachine[HalfWidth] ;
        }
    if (Max) printf ("%d: %d (#%d)\n", 2 * HalfWidth + 1, Max, MaxMachineIndex) ;
    }

  uint32_t TotalMatches = 0 ;
  for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
    TotalMatches += DeciderArray[i] -> TotalMatches ;
  printf ("Total matches = %d\n", TotalMatches) ;

  int MinStat = INT_MAX ;
  uint32_t MinStatMachine = 0 ;
  int MaxStat = INT_MIN ;
  uint32_t MaxStatMachine = 0 ;
  for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
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

    MachineSpecList += MACHINE_SPEC_SIZE ;
    VerificationEntryList += VERIF_ENTRY_LENGTH ;
    }
  }

bool HaltingSegment::RunDecider (const uint8_t* MachineSpec)
  {
  for (int i = 0 ; i <= NSTATES ; i++) TransitionTable[i].clear() ;

  for (int i = 0 ; i <= 1 ; i++)
    {
    LeftOfSegment[i].clear() ;
    RightOfSegment[i].clear() ;
    }

  // Build the backward transition table from the MachineSpec
  for (uint8_t State = 1 ; State <= NSTATES ; State++)
    for (uint8_t Cell = 0 ; Cell <= 1 ; Cell++)
      {
      Transition T ;
      T.State = State ;
      T.Read = Cell ;
      T.Write = *MachineSpec++ ;
      T.Move = *MachineSpec++ ;
      uint8_t Next = *MachineSpec++ ;

      TransitionTable[Next].push_back (T) ;

      if (Next != 0)
        {
        if (T.Move) LeftOfSegment[T.Write].push_back (T) ;
        else RightOfSegment[T.Write].push_back (T) ;
        }
      }

  for (HalfWidth = 1 ; 2 * HalfWidth + 1 <= CommandLineParams::WidthLimit ; HalfWidth++)
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
    nMatches = 0 ;

    if (Recurse (0, StartConfig))
      {
      if (MaxDepth > MaxDecidingDepth[HalfWidth])
        {
        MaxDecidingDepth[HalfWidth] = MaxDepth ;
        MaxDecidingDepthMachine[HalfWidth] = SeedDatabaseIndex ;
        }
      if ((int)nMatches > MaxStat)
        {
        MaxStat = nMatches ;
        MaxStatMachine = SeedDatabaseIndex ;
        }
      TotalMatches += nMatches ;
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
    if (CommandLineParams::TraceOutput)
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
   if (Depth > 150000) return false ;
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

  if (CommandLineParams::TraceOutput)
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

  if (CommandLineParams::TraceOutput)
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

uint32_t HaltingSegment::FindShorterOrEqual (const CompoundTree* Tree, const uint8_t* TapeHead)
  {
  if (Tree == nullptr) return 0 ;
  for (const uint8_t* p = TapeHead - 1 ; Tree ; p--)
    {
    uint32_t NodeIndex = FindShorterOrEqual (Tree -> SubTree, TapeHead + 1) ;
    if (NodeIndex) return NodeIndex ;
    if (*p > 1) return 0 ;
    Tree = Tree -> Next[*p] ;
    }
  return 0 ;
  }

HaltingSegment::CompoundTree* HaltingSegment::Insert (CompoundTree* Tree, const uint8_t* TapeHead, uint32_t NodeIndex)
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

uint32_t HaltingSegment::FindShorterOrEqual (const ForwardTree* Tree, const uint8_t* TapeHead)
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

HaltingSegment::ForwardTree* HaltingSegment::Insert (ForwardTree* Tree, const uint8_t* TapeHead, uint32_t NodeIndex)
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

uint32_t HaltingSegment::FindShorterOrEqual (const BackwardTree* Tree, const uint8_t* TapeHead)
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

HaltingSegment::BackwardTree* HaltingSegment::Insert (BackwardTree* Tree, const uint8_t* TapeHead, uint32_t NodeIndex)
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
    if (argv[0][0] != '-') printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
    switch (toupper (argv[0][1]))
      {
      case 'S':
        if (argv[0][2] == 0) printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
        SeedDatabaseFile = std::string (&argv[0][2]) ;
        break ;

      case 'I':
        if (argv[0][2] == 0) printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
        InputFile = std::string (&argv[0][2]) ;
        break ;

      case 'V':
        if (argv[0][2] == 0) printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
        VerificationFile = std::string (&argv[0][2]) ;
        break ;

      case 'U':
        if (argv[0][2] == 0) printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
        UndecidedFile = std::string (&argv[0][2]) ;
        break ;

      case 'W':
        WidthLimit = atoi (&argv[0][2]) ;
        if (!(WidthLimit & 1)) printf ("Segment width limit must be odd\n"), exit (1) ;
        WidthLimitPresent = true ;
        break ;

      case 'M':
        nThreads = atoi (&argv[0][2]) ;
        nThreadsPresent = true ;
        break ;

      case 'X':
        TestMachine = atoi (&argv[0][2]) ;
        TestMachinePresent = true ;
        break ;

      case 'T':
        TraceOutput = true ;
        break ;

      case 'L':
        MachineLimit = atoi (&argv[0][2]) ;
        MachineLimitPresent = true ;
        break ;

      default:
        printf ("Invalid parameter \"%s\"\n", argv[0]) ;
        PrintHelpAndExit (1) ;
      }
    }

  if (!TestMachinePresent && InputFile.empty())
    printf ("Input file not specified\n"), PrintHelpAndExit (1) ;

  if (!WidthLimitPresent) printf ("Width limit not specified\n"), PrintHelpAndExit (1) ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf (R"*RAW*(
HaltingSegments <param> <param>...
  <param>: -S<seed database>      Seed database file (defaults to ../SeedDatabase.bin)
           -I<input file>         Input file: list of machines to be analysed
           -V<verification file>  Output file: verification data for decided machines
           -U<undecided file>     Output file: remaining undecided machines
           -W<width limit>        Max segment width (must be odd)
           -M<threads>            Number of threads to run
           -X<test machine>       Machine to test
           -T                     Print trace output
           -L<machine limit>      Max no. of machines to test
)*RAW*") ;
  exit (status) ;
  }
