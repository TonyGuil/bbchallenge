// HaltingSegments <param> <param>...
//
//   <param>: -S<seed database>      Seed database file (defaults to ../SeedDatabase.bin)
//            -I<input file>         Input file: list of machines to be analysed
//            -V<verification file>  Output file: verification data for decided machines
//            -U<undecided file>     Output file: remaining undecided machines
//            -D<depth limit>        Max search depth
//            -W<width limit>        Max absolute value of tape head
//            -N<node limit>         Max number of nodes in search
//            -M<threads>            Number of threads to run
//            -X<test machine>       Machine to test
//            -T                     Print trace output
//
// The HaltingSegments Decider starts from the HALT state and recursively generates 
// all possible predecessor states within a given tape window, plus all possible
// predecessor states that could have left the window to the left or right before
// entering it again. If it can determine that all possible states lie within a
// distance DepthLimit of the HALT state, and none of these states is the starting
// state, then there is no way to reach the HALT state from the starting state, and
// the machine can be flagged as non-halting.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>
#include <set>
#include <boost/thread.hpp>

#include "..\bbchallenge.h"

#define CHUNK_SIZE 256 // Number of machines to assign to each thread

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
  static uint32_t DepthLimit ;    static bool DepthLimitPresent ;
  static uint32_t WidthLimit ;    static bool WidthLimitPresent ;
  static uint32_t NodeLimit ;     static bool NodeLimitPresent ;
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
uint32_t CommandLineParams::DepthLimit ;    bool CommandLineParams::DepthLimitPresent ;
uint32_t CommandLineParams::WidthLimit ;    bool CommandLineParams::WidthLimitPresent ;
uint32_t CommandLineParams::NodeLimit ;     bool CommandLineParams::NodeLimitPresent ;
uint32_t CommandLineParams::nThreads ;      bool CommandLineParams::nThreadsPresent ;
uint32_t CommandLineParams::TestMachine ;   bool CommandLineParams::TestMachinePresent ;
bool CommandLineParams::TraceOutput ;
uint32_t CommandLineParams::MachineLimit ;  bool CommandLineParams::MachineLimitPresent ;

//
// HaltingSegments class
//

class HaltingSegments
  {
public:
  HaltingSegments (uint32_t DepthLimit, uint32_t SpaceLimit, uint32_t NodeLimit)
  : DepthLimit (DepthLimit), SpaceLimit (SpaceLimit), NodeLimit (NodeLimit)
    {
    // Allocate the tape workspace
    Tape = new uint8_t[2 * SpaceLimit + 1] ;
    Tape[0] = Tape[2 * SpaceLimit] = TAPE_SENTINEL ;
    Tape += SpaceLimit ; // so Tape[0] is in the middle

    // Reserve maximum possible lengths for the backward transition vectors,
    // to avoid having to re-allocate them in the middle of the search (a
    // mini-optimisation)
    for (int i = 0 ; i <= NSTATES ; i++)
      TransitionTable[i].reserve (2 * NSTATES) ;

    // Initialise the tree pools
    CompoundTreePool.Initialise (3 * NodeLimit * SpaceLimit) ;
    SimpleTreePool.Initialise (2 * NodeLimit * SpaceLimit) ;

    // Statistics
    MaxDecidingDepth = new uint32_t[SpaceLimit + 1] ;
    memset (MaxDecidingDepth, 0, (SpaceLimit + 1) * sizeof (uint32_t)) ;
    }

  // Call Run to analyse a single machine. MachineSpec is in the 30-byte
  // Seed Database format:
  bool Run (const uint8_t* MachineSpec) ;

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

  #define LEAF_NODE (void*)1

  struct SimpleTree { SimpleTree* Next[2] ; } ;
  struct ForwardTree { ForwardTree* Next[2] ; } ;
  struct BackwardTree { BackwardTree* Next[2] ; } ;
  struct CompoundTree
    {
    CompoundTree* Next[2] ;
    ForwardTree* SubTree ;
    } ;

  bool FindShorterOrEqual (const CompoundTree* Tree, const uint8_t* TapeHead) ;
  CompoundTree* Insert (CompoundTree* Tree, const uint8_t* TapeHead) ;

  bool FindShorterOrEqual (const ForwardTree* subTree, const uint8_t* TapeHead) ;
  ForwardTree* Insert (ForwardTree* Tree, const uint8_t* TapeHead) ;

  bool FindShorterOrEqual (const BackwardTree* subTree, const uint8_t* TapeHead) ;
  BackwardTree* Insert (BackwardTree* Tree, const uint8_t* TapeHead) ;

  template<class TreeType> class TreePool
    {
  public:
    TreeType* Pool ;
    TreeType* EndOfPool ;
    TreeType* HighWater ;
    TreeType* FreeChain ;

    void Initialise (size_t Size)
      {
      Pool = new TreeType[Size] ;
      EndOfPool = Pool + Size ;
      Clear() ;
      }

    void Clear()
      {
      HighWater = Pool ;
      FreeChain = nullptr ;
      }

    TreeType* Allocate()
      {
      if (HighWater < EndOfPool) return HighWater++ ;
      if (FreeChain)
        {
        TreeType* Tree = FreeChain ;
        FreeChain = FreeChain -> Next[0] ;
        return Tree ;
        }
      printf ("Pool exhaustion (%d exceeded)\n", EndOfPool - Pool), exit (1) ;
      }

    void Free (TreeType* Tree)
      {
      Tree -> Next[0] = FreeChain ;
      FreeChain = Tree ;
      }

    void RemoveSubNodes (TreeType* Tree)
      {
      if (Tree == 0 || Tree == LEAF_NODE) return ;
      RemoveSubNodes (Tree -> Next[0]) ;
      RemoveSubNodes (Tree -> Next[1]) ;
      Free (Tree) ;
      }
    } ;

  CompoundTree* AlreadySeen[6][2] ;
  ForwardTree* ExitedLeft ;
  BackwardTree* ExitedRight ;

  TreePool<CompoundTree> CompoundTreePool ;
  TreePool<SimpleTree> SimpleTreePool ;

  uint32_t DepthLimit ;
  uint32_t SpaceLimit ;
  uint32_t NodeLimit ;

  uint32_t HalfWidth ; // Max absolute value of TapeHead

  //
  // STATISTICS
  //

  int Leftmost, Rightmost ;
  uint32_t MaxDepth ;
  uint32_t nNodes ;
  uint32_t* MaxDecidingDepth ;
  } ;

int main (int argc, char** argv)
  {
  CommandLineParams::Parse (argc, argv) ;

  TuringMachineReader Reader (CommandLineParams::SeedDatabaseFile.c_str()) ;

  uint8_t MachineSpec[MACHINE_SPEC_SIZE] ;
  if (CommandLineParams::TestMachinePresent)
    {
    HaltingSegments Decider (CommandLineParams::DepthLimit,
      CommandLineParams::WidthLimit, CommandLineParams::NodeLimit) ;
    Decider.SeedDatabaseIndex = CommandLineParams::TestMachine ;
    Reader.Read (Decider.SeedDatabaseIndex, MachineSpec) ;
    printf ("%d\n", Decider.Run (MachineSpec)) ;
    printf ("%d %d\n", Decider.MaxDepth, Decider.nNodes) ;
    exit (0) ;
    }

  // fpin contains the list of machines to analyse
  FILE* fpin = fopen (CommandLineParams::InputFile.c_str(), "rb") ;
  if (fpin == NULL) printf ("Can't open input file\n"), exit (1) ;
  if (fseek (fpin, 0, SEEK_END))
    printf ("fseek failed\n"), exit (1) ;
  uint32_t InputFileSize = ftell (fpin) ;
  if (fseek (fpin, 0, SEEK_SET))
    printf ("fseek failed\n"), exit (1) ;

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

  uint32_t nTimeLimited = Read32 (fpin) ;
  uint32_t nSpaceLimited = Read32 (fpin) ;
  uint32_t nTotal = nTimeLimited + nSpaceLimited ;

  if (InputFileSize != 4 * (nTotal + 2))
    printf ("File size discrepancy\n"), exit (1) ;

  // Write dummy headers
  Write32 (fpUndecided, 0) ;
  Write32 (fpUndecided, 0) ;
  Write32 (fpVerif, 0) ;

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

  clock_t Timer = clock() ;

  HaltingSegments** DeciderArray = new HaltingSegments*[CommandLineParams::nThreads] ;
  uint32_t** MachineIndexList = new uint32_t*[CommandLineParams::nThreads] ;
  uint8_t** MachineSpecList = new uint8_t*[CommandLineParams::nThreads] ;
  uint8_t** VerificationEntryList = new uint8_t*[CommandLineParams::nThreads] ;
  uint32_t* ChunkSize = new uint32_t[CommandLineParams::nThreads] ;
  for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
    {
    DeciderArray[i] = new HaltingSegments (CommandLineParams::DepthLimit,
      CommandLineParams::WidthLimit, CommandLineParams::NodeLimit) ;
    MachineIndexList[i] = new uint32_t[CHUNK_SIZE] ;
    MachineSpecList[i] = new uint8_t[MACHINE_SPEC_SIZE * CHUNK_SIZE] ;
    VerificationEntryList[i] = new uint8_t[VERIF_ENTRY_LENGTH * CHUNK_SIZE] ;
    }

  uint32_t nDecided = 0 ;
  uint32_t nTimeLimitedDecided = 0 ;
  uint32_t nSpaceLimitedDecided = 0 ;
  uint32_t nCompleted = 0 ;
  int LastPercent = -1 ;

  if (CommandLineParams::MachineLimitPresent) nTotal = CommandLineParams::MachineLimit ;
  while (nCompleted < nTotal)
    {
    uint32_t nRemaining = nTotal - nCompleted ;
    if (nRemaining >= CommandLineParams::nThreads * CHUNK_SIZE)
      {
      for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++) ChunkSize[i] = CHUNK_SIZE ;
      }
    else
      {
      for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
        {
        ChunkSize[i] = nRemaining / (CommandLineParams::nThreads - i) ;
        nRemaining -= ChunkSize[i] ;
        }
      }

    std::vector<boost::thread*> ThreadList (CommandLineParams::nThreads) ;
    for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
      {
      for (uint32_t j = 0 ; j < ChunkSize[i] ; j++)
        {
        MachineIndexList[i][j] = Read32 (fpin) ;
        Reader.Read (MachineIndexList[i][j], MachineSpecList[i] + j * MACHINE_SPEC_SIZE) ;
        }

      ThreadList[i] = new boost::thread (HaltingSegments::ThreadFunction,
        DeciderArray[i], ChunkSize[i], MachineIndexList[i], MachineSpecList[i], VerificationEntryList[i]) ;
      }

    for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
      {
      // Wait for thread i to finish
      ThreadList[i] -> join() ;
      delete ThreadList[i] ;

      const uint8_t* MachineSpec = MachineSpecList[i] ;
      const uint8_t* VerificationEntry = VerificationEntryList[i] ;
      for (uint32_t j = 0 ; j < ChunkSize[i] ; j++)
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

    int Percent = (nCompleted * 100LL) / nTotal ;
    if (Percent != LastPercent)
      {
      LastPercent = Percent ;
      printf ("\r%d%% %d %d", Percent, nCompleted, nDecided) ;
      }
    }

  // Check that we've reached the end of the input file
  if (!CommandLineParams::MachineLimitPresent && fread (MachineSpec, 1, 1, fpin) != 0)
    printf ("\nInput file too long!\n"), exit (1) ;

  if (fpVerif)
    {
    // Write the verification file header
    if (fseek (fpVerif, 0 , SEEK_SET))
      printf ("\nfseek failed\n"), exit (1) ;
    Write32 (fpVerif, nDecided) ;
    fclose (fpVerif) ;
    }

  Timer = clock() - Timer ;

  printf ("\n") ;

  if (fpUndecided)
    {
    // Write the undecided file header
    if (fseek (fpUndecided, 0 , SEEK_SET))
      printf ("\nfseek failed\n"), exit (1) ;
    Write32 (fpUndecided, nTimeLimited - nTimeLimitedDecided) ;
    Write32 (fpUndecided, nSpaceLimited - nSpaceLimitedDecided) ;

    fclose (fpUndecided) ;
    }
  fclose (fpin) ;

  printf ("\nDecided %d out of %d\n", nDecided, nTimeLimited + nSpaceLimited) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;

  printf ("\nMax search depth for decided machines by segment width:") ;
  for (uint32_t HalfWidth = 3 ; 2 * HalfWidth <= CommandLineParams::WidthLimit + 1 ; HalfWidth++)
    {
    uint32_t Max = 0 ;
    for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
      if (DeciderArray[i] -> MaxDecidingDepth[HalfWidth] > Max)
        Max = DeciderArray[i] -> MaxDecidingDepth[HalfWidth] ;
    if (Max) printf ("%d: %d\n", 2 * HalfWidth - 1, Max) ;
    }
  }

void HaltingSegments::ThreadFunction (int nMachines, const uint32_t* MachineIndexList,
  const uint8_t* MachineSpecList, uint8_t* VerificationEntryList)
  {
  while (nMachines--)
    {
    SeedDatabaseIndex = *MachineIndexList++ ;
    if (Run (MachineSpecList))
      {
      Save32 (VerificationEntryList, SeedDatabaseIndex) ;
      Save32 (VerificationEntryList + 4, uint32_t (DeciderTag::HALTING_SEGMENTS)) ;
      Save32 (VerificationEntryList + 8, VERIF_INFO_LENGTH) ;
      Save32 (VerificationEntryList + 12, Leftmost) ;
      Save32 (VerificationEntryList + 16, Rightmost) ;
      Save32 (VerificationEntryList + 20, MaxDepth) ;
      Save32 (VerificationEntryList + 24, nNodes) ;
      Save32 (VerificationEntryList + 28, 2 * HalfWidth - 1) ;
      }
    else Save32 (VerificationEntryList + 4, uint32_t (DeciderTag::NONE)) ;

    MachineSpecList += MACHINE_SPEC_SIZE ;
    VerificationEntryList += VERIF_ENTRY_LENGTH ;
    }
  }

bool HaltingSegments::Run (const uint8_t* MachineSpec)
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

  for (HalfWidth = 3 ; 2 * HalfWidth <= CommandLineParams::WidthLimit + 1 ; HalfWidth++)
    {
    // Start in state 0 with unspecified tape
    memset (Tape - HalfWidth + 1, TAPE_ANY, 2 * HalfWidth - 1) ;
    Tape[-HalfWidth] = TAPE_SENTINEL_LEFT ;
    Tape[HalfWidth] = TAPE_SENTINEL_RIGHT ;
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
        MaxDecidingDepth[HalfWidth] = MaxDepth ;
      return true ;
      }
    }

  return false ;
  }

bool HaltingSegments::Recurse (uint32_t Depth, const Configuration& Config)
  {
  if (Depth == DepthLimit) return false ; // Search too deep, no decision possible
  if (++Depth > MaxDepth) MaxDepth = Depth ;

  // Check for possible match with starting configuration
  if (Config.State == 1)
    {
    int i ; for (i = 1 - HalfWidth ; i < int(HalfWidth) ; i++)
      if (Tape[i] != 0 && Tape[i] != TAPE_ANY) break ;
    if (i >= (int)HalfWidth) return false ;
    }

  if (++nNodes > NodeLimit) return false ;

  if (CommandLineParams::TraceOutput)
    {
    for (int i = 1 - HalfWidth ; i < (int)HalfWidth ; i++)
      {
      printf (i == Config.TapeHead ? "[" : i == Config.TapeHead + 1 ? "]" : " ") ;
      printf ("%c", "01*."[Tape[i]]) ;
      }
    printf (Config.TapeHead == (int)(HalfWidth - 1) ? "]" : " ") ;
    printf ("%*s%c\n", Depth, "", Config.State + '@') ;
    }

  // If we've seen this already, return true
  if (Tape[Config.TapeHead] <= 1)
    {
    CompoundTree*& Tree = AlreadySeen[Config.State][Tape[Config.TapeHead]] ;
    if (FindShorterOrEqual (Tree, Tape + Config.TapeHead))
      {
      if (CommandLineParams::TraceOutput) printf ("Duplicate\n") ;
      return true ;
      }

    Tree = Insert (Tree, Tape + Config.TapeHead) ;
    }

  Configuration PrevConfig ;
  for (const auto& T : TransitionTable[Config.State])
    {
    // Update the tape head
    if (T.Move)
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
        if (!ExitSegmentLeft (Depth, Config.State)) return false ;
        continue ;

      case TAPE_SENTINEL_RIGHT: // Exiting tape segment to the right
        if (!ExitSegmentRight (Depth, Config.State)) return false ;
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

bool HaltingSegments::ExitSegmentLeft (uint32_t Depth, uint8_t State)
  {
  if (Depth == DepthLimit) return false ; // Search too deep, no decision possible
  if (++Depth > MaxDepth) MaxDepth = Depth ;

  if (++nNodes > NodeLimit) return false ;

  if (CommandLineParams::TraceOutput)
    {
    for (int i = 1 - HalfWidth ; i < (int)HalfWidth ; i++)
      printf (" %c", "01*."[Tape[i]]) ;
    printf (" %*s*\n", Depth, "") ;
    }

  // Check for all zeroes or unset
  int i ; for (i = 1 - HalfWidth ; i < int(HalfWidth) ; i++)
    if (Tape[i] != 0 && Tape[i] != TAPE_ANY) break ;
  if (i >= (int)HalfWidth) return false ;

  // If we've seen this already, return true
  if (FindShorterOrEqual (ExitedLeft, Tape - HalfWidth + 1))
    {
    if (CommandLineParams::TraceOutput) printf ("Left duplicate\n") ;
    return true ;
    }

  ExitedLeft = Insert (ExitedLeft, Tape - HalfWidth + 1) ;

  Configuration PrevConfig ;
  PrevConfig.TapeHead = 1 - HalfWidth ;
  uint8_t Cell = Tape[1 - HalfWidth] ;
  for (const auto& T : LeftOfSegment[Cell])
    {
    PrevConfig.State = T.State ;
    Tape[1 - HalfWidth] = T.Read ;
    if (!Recurse (Depth, PrevConfig)) return false ;
    Tape[1 - HalfWidth] = Cell ;
    }

  return true ;
  }

bool HaltingSegments::ExitSegmentRight (uint32_t Depth, uint8_t State)
  {
  if (Depth == DepthLimit) return false ; // Search too deep, no decision possible
  if (++Depth > MaxDepth) MaxDepth = Depth ;

  if (++nNodes > NodeLimit) return false ;

  if (CommandLineParams::TraceOutput)
    {
    for (int i = 1 - HalfWidth ; i < (int)HalfWidth ; i++)
      printf (" %c", "01*."[Tape[i]]) ;
    printf (" %*s*\n", Depth, "") ;
    }

  // Check for all zeroes or unset
  int i ; for (i = 1 - HalfWidth ; i < int(HalfWidth) ; i++)
    if (Tape[i] != 0 && Tape[i] != TAPE_ANY) break ;
  if (i >= (int)HalfWidth) return false ;

  // If we've seen this already, return true
  if (FindShorterOrEqual (ExitedRight, Tape + HalfWidth - 1))
    {
    if (CommandLineParams::TraceOutput) printf ("Right duplicate\n") ;
    return true ;
    }

  ExitedRight = Insert (ExitedRight, Tape + HalfWidth - 1) ;

  Configuration PrevConfig ;
  PrevConfig.TapeHead = HalfWidth - 1 ;
  uint8_t Cell = Tape[HalfWidth - 1] ;
  for (const auto& T : RightOfSegment[Cell])
    {
    PrevConfig.State = T.State ;
    Tape[HalfWidth - 1] = T.Read ;
    if (!Recurse (Depth, PrevConfig)) return false ;
    Tape[HalfWidth - 1] = Cell ;
    }

  return true ;
  }

bool HaltingSegments::FindShorterOrEqual (const CompoundTree* Tree, const uint8_t* TapeHead)
  {
  if (Tree == nullptr) return false ;
  for (const uint8_t* p = TapeHead - 1 ; Tree ; p--)
    {
    if (FindShorterOrEqual (Tree -> SubTree, TapeHead + 1)) return true ;
    if (*p > 1) return false ;
    Tree = Tree -> Next[*p] ;
    }
  return false ;
  }

HaltingSegments::CompoundTree* HaltingSegments::Insert (CompoundTree* Tree, const uint8_t* TapeHead)
  {
  if (Tree == 0)
    {
    Tree = CompoundTreePool.Allocate() ;
    Tree -> Next[0] = Tree -> Next[1] = 0 ;
    Tree -> SubTree = 0 ;
    }
  CompoundTree* Node = Tree ;
  for (const uint8_t* p = TapeHead - 1 ; *p <= 1 ; p--)
    {
    if (Node -> Next[*p] == 0)
      {
      Node -> Next[*p] = CompoundTreePool.Allocate() ;
      Node -> Next[*p] -> Next[0] = Node -> Next[*p] -> Next[1] = 0 ;
      Node -> Next[*p] -> SubTree = 0 ;
      }
    Node = Node -> Next[*p] ;
    }
  Node -> SubTree = Insert (Node -> SubTree, TapeHead + 1) ;
  return Tree ;
  }

bool HaltingSegments::FindShorterOrEqual (const ForwardTree* Tree, const uint8_t* TapeHead)
  {
  // Tree = 0 means no entries here:
  if (Tree == 0) return false ;
  if (Tree == LEAF_NODE) return true ;

  for ( ; ; TapeHead++)
    {
    if (*TapeHead > 1) return false ;
    Tree = Tree -> Next[*TapeHead] ;
    if (Tree == 0) return false ;
    if (Tree == LEAF_NODE) return true ;
    if (Tree -> Next[0] == 0 && Tree -> Next[1] == 0)
      printf ("Error 2 in FindShorterOrEqual (ForwardTree)\n"), exit (1) ;
    }
  }

HaltingSegments::ForwardTree* HaltingSegments::Insert (ForwardTree* Tree, const uint8_t* TapeHead)
  {
  if (*TapeHead > 1) return (ForwardTree*)LEAF_NODE ; // Empty string

  if (Tree == 0)
    {
    Tree = (ForwardTree*)SimpleTreePool.Allocate() ;
    Tree -> Next[0] = Tree -> Next[1] = 0 ;
    }
  else if (Tree -> Next[0] == 0 && Tree -> Next[1] == 0)
    printf ("Error 2 in Insert (ForwardTree)\n"), exit (1) ;

  ForwardTree* Node = Tree ;
  for ( ; ; )
    {
    if (TapeHead[1] > 1)
      {
      Node -> Next[*TapeHead] = (ForwardTree*)LEAF_NODE ;
      return Tree ;
      }
    if (Node -> Next[*TapeHead] == 0)
      {
      Node -> Next[*TapeHead] = (ForwardTree*)SimpleTreePool.Allocate() ;
      Node -> Next[*TapeHead] -> Next[0] = Node -> Next[*TapeHead] -> Next[1] = 0 ;
      }
    else if (Node -> Next[*TapeHead] == LEAF_NODE)
      printf ("Error 1 in Insert (ForwardTree)\n"), exit (1) ;

    Node = Node -> Next[*TapeHead++] ;
    }
  }

bool HaltingSegments::FindShorterOrEqual (const BackwardTree* Tree, const uint8_t* TapeHead)
  {
  // Tree = 0 means no entries here:
  if (Tree == 0) return false ;
  if (Tree == LEAF_NODE) return true ;

  for ( ; ; TapeHead--)
    {
    if (*TapeHead > 1) return false ;
    Tree = Tree -> Next[*TapeHead] ;
    if (Tree == 0) return false ;
    if (Tree == LEAF_NODE) return true ;
    if (Tree -> Next[0] == 0 && Tree -> Next[1] == 0)
      printf ("Error 2 in FindShorterOrEqual (BackwardTree)\n"), exit (1) ;
    }
  }

HaltingSegments::BackwardTree* HaltingSegments::Insert (BackwardTree* Tree, const uint8_t* TapeHead)
  {
#if PRUNT
  Tree = RemoveLongerOrEqual (Tree, TapeHead) ;
#endif

  if (*TapeHead > 1) return (BackwardTree*)LEAF_NODE ; // Empty string

  if (Tree == 0)
    {
    Tree = (BackwardTree*)SimpleTreePool.Allocate() ;
    Tree -> Next[0] = Tree -> Next[1] = 0 ;
    }
  else if (Tree -> Next[0] == 0 && Tree -> Next[1] == 0)
    printf ("Error 2 in Insert (BackwardTree)\n"), exit (1) ;

  BackwardTree* Node = Tree ;
  for ( ; ; )
    {
    if (TapeHead[-1] > 1)
      {
      Node -> Next[*TapeHead] = (BackwardTree*)LEAF_NODE ;
      return Tree ;
      }
    if (Node -> Next[*TapeHead] == 0)
      {
      Node -> Next[*TapeHead] = (BackwardTree*)SimpleTreePool.Allocate() ;
      Node -> Next[*TapeHead] -> Next[0] = Node -> Next[*TapeHead] -> Next[1] = 0 ;
      }
    else if (Node -> Next[*TapeHead] == LEAF_NODE)
      printf ("Error 1 in Insert (BackwardTree)\n"), exit (1) ;

    Node = Node -> Next[*TapeHead--] ;
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

      case 'D':
        DepthLimit = atoi (&argv[0][2]) ;
        DepthLimitPresent = true ;
        break ;

      case 'W':
        WidthLimit = atoi (&argv[0][2]) ;
        if (!(WidthLimit & 1)) printf ("Segment width limit must be odd\n"), exit (1) ;
        WidthLimitPresent = true ;
        break ;

      case 'N':
        NodeLimit = atoi (&argv[0][2]) ;
        NodeLimitPresent = true ;
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

  if (!DepthLimitPresent) printf ("Depth limit not specified\n"), PrintHelpAndExit (1) ;
  if (!WidthLimitPresent) printf ("HalfWidth limit not specified\n"), PrintHelpAndExit (1) ;
  if (!NodeLimitPresent) printf ("Node limit not specified\n"), PrintHelpAndExit (1) ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf (R"*RAW*(
HaltingSegments <param> <param>...
  <param>: -S<seed database>      Seed database file (defaults to ../SeedDatabase.bin)
           -I<input file>         Input file: list of machines to be analysed
           -V<verification file>  Output file: verification data for decided machines
           -U<undecided file>     Output file: remaining undecided machines
           -D<depth limit>        Max search depth
           -W<width limit>        Max segment width (must be odd)
           -N<node limit>         Max number of nodes in search
           -M<threads>            Number of threads to run
           -X<test machine>       Machine to test
           -T                     Print trace output
           -L<machine limit>      Max no. of machines to test
)*RAW*") ;
  exit (status) ;
  }
