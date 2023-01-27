// Cyclers <input file> <output file> <time> <space>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <boost/thread.hpp>

#include "../bbchallenge.h"

#define CHUNK_SIZE 1024 // Number of machines to assign to each thread

#define VERIF_INFO_LENGTH 24 // Length of DeciderSpecificInfo in Verification File

class CommandLineParams
  {
public:
  static std::string SeedDatabaseFile ;
  static std::string VerificationFile ;
  static std::string UndecidedFile ;
  static uint32_t TimeLimit ;     static bool TimeLimitPresent ;
  static uint32_t SpaceLimit ;    static bool SpaceLimitPresent ;
  static uint32_t nThreads ;      static bool nThreadsPresent ;
  static void Parse (int argc, char** argv) ;
  static void PrintHelpAndExit [[noreturn]] (int status) ;
  } ;

std::string CommandLineParams::SeedDatabaseFile ;
std::string CommandLineParams::VerificationFile ;
std::string CommandLineParams::UndecidedFile ;
uint32_t CommandLineParams::TimeLimit ;     bool CommandLineParams::TimeLimitPresent ;
uint32_t CommandLineParams::SpaceLimit ;    bool CommandLineParams::SpaceLimitPresent ;
uint32_t CommandLineParams::nThreads ;      bool CommandLineParams::nThreadsPresent ;

class Cycler : public TuringMachine
  {
public:
  Cycler (uint32_t TimeLimit, uint32_t SpaceLimit)
  : TuringMachine (SpaceLimit)
  , TimeLimit (TimeLimit)
    {
    HistoryWorkspace = new uint8_t[(2 * SpaceLimit + 1) * TimeLimit] ;

    TapeHistory = new uint8_t*[TimeLimit] ;
    for (uint32_t i = 0 ; i < TimeLimit ; i++)
      TapeHistory[i] = HistoryWorkspace + i * (2 * SpaceLimit + 1) ;

    // For each combination of State and TapeHead, we maintain a chain of
    // configurations, so that we only have to compare tape contents for
    // a fraction of previous configurations:
    PreviousConfig = new int[TimeLimit] ;
    PreviousWorkspace = new int[NSTATES * (2 * SpaceLimit + 1)] ;
    Previous[0] = 0 ;
    Previous[1] = PreviousWorkspace + SpaceLimit ;
    for (int i = 2 ; i <= NSTATES ; i++)
      Previous[i] = Previous[i - 1] + 2 * SpaceLimit + 1 ;
    }

  void ThreadFunction (uint32_t MachineIndex, int nMachines, const uint8_t* MachineSpecList,
    uint8_t* VerificationEntryList) ;

private:

  void Run (uint32_t MachineIndex, const uint8_t* MachineSpec, uint8_t* VerificationEntry) ;

  uint32_t TimeLimit ;

  uint8_t* HistoryWorkspace ;
  uint8_t** TapeHistory ;

  int* PreviousConfig ;
  int* PreviousWorkspace ;
  int* Previous[NSTATES + 1] ;
  } ;

int main (int argc, char** argv)
  {
  CommandLineParams::Parse (argc, argv) ;

  TuringMachineReader Reader (CommandLineParams::SeedDatabaseFile.c_str()) ;

  FILE* fpUndecided = fopen (CommandLineParams::UndecidedFile.c_str(), "wb") ;
  if (fpUndecided == NULL)
    printf ("Can't open output file \"%s\"\n", CommandLineParams::UndecidedFile.c_str()), exit (1) ;

  FILE* fpVerif = fopen (CommandLineParams::VerificationFile.c_str(), "wb") ;
  if (fpVerif == NULL)
    printf ("Can't open output file \"%s\"\n", CommandLineParams::VerificationFile.c_str()), exit (1) ;

  // Write dummy dvf header
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

  Cycler** CyclerArray = new Cycler*[CommandLineParams::nThreads] ;
  uint8_t** MachineSpecList = new uint8_t*[CommandLineParams::nThreads] ;
  uint8_t** VerificationEntryList = new uint8_t*[CommandLineParams::nThreads] ;
  uint32_t* ChunkSize = new uint32_t[CommandLineParams::nThreads] ;

  // Allocate the per-thread workspace
  for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
    {
    CyclerArray[i] = new Cycler (CommandLineParams::TimeLimit, CommandLineParams::SpaceLimit) ;
    MachineSpecList[i] = new uint8_t[MACHINE_SPEC_SIZE * CHUNK_SIZE] ;
    VerificationEntryList[i] = new uint8_t[VERIF_ENTRY_LENGTH * CHUNK_SIZE] ;
    }

  uint32_t nDecided = 0 ;
  int LastPercent = -1 ;
  uint32_t nCompleted = 0 ;

  // Analyse the time-limited machines in the seed database
  while (nCompleted < Reader.nTimeLimited)
    {
    uint32_t nRemaining = Reader.nTimeLimited - nCompleted ;
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
    uint32_t MachineIndex = nCompleted ;
    for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
      {
      Reader.Read (MachineIndex, MachineSpecList[i], ChunkSize[i]) ;
      ThreadList[i] = new boost::thread (&Cycler::ThreadFunction,
        CyclerArray[i], MachineIndex, ChunkSize[i], MachineSpecList[i], VerificationEntryList[i]) ;
      MachineIndex += ChunkSize[i] ;
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
          if (fwrite (VerificationEntry, VERIF_ENTRY_LENGTH, 1, fpVerif) != 1)
            printf ("Error writing file\n"), exit (1) ;
          nDecided++ ;
          }
        else Write32 (fpUndecided, nCompleted) ;
        MachineSpec += MACHINE_SPEC_SIZE ;
        VerificationEntry += VERIF_ENTRY_LENGTH ;
        nCompleted++ ;
        }
      }

    int Percent = (nCompleted * 100LL) / Reader.nTimeLimited ;
    if (Percent != LastPercent)
      {
      LastPercent = Percent ;
      printf ("\r%d%% %d %d", Percent, nCompleted, nDecided) ;
      fflush (stdout) ;
      }
    }

  if (fseek (fpVerif, 0 , SEEK_SET))
    printf ("\nfseek failed\n"), exit (1) ;
  Write32 (fpVerif, nDecided) ;
  fclose (fpVerif) ;

  Timer = clock() - Timer ;

  printf ("\r100%% %d %d\n", nCompleted, nDecided) ;

  // The space-limited machines are not expected to yield any Cyclers,
  // so we just copy these to the umf
  printf ("Copying space-limited machines...") ;
  fflush (stdout) ;
  for (uint32_t i = Reader.nTimeLimited ; i < Reader.nMachines ; i++)
    Write32 (fpUndecided, i) ;
  printf ("Done\n") ;

  fclose (fpUndecided) ;

  printf ("\nDecided %d out of %d\n", nDecided, Reader.nMachines) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;
  }

void Cycler::ThreadFunction (uint32_t MachineIndex, int nMachines, const uint8_t* MachineSpecList,
  uint8_t* VerificationEntryList)
  {
  while (nMachines--)
    {
    Run (MachineIndex++, MachineSpecList, VerificationEntryList) ;
    MachineSpecList += MACHINE_SPEC_SIZE ;
    VerificationEntryList += VERIF_ENTRY_LENGTH ;
    }
  }

void Cycler::Run (uint32_t MachineIndex, const uint8_t* MachineSpec, uint8_t* VerificationEntry)
  {
  Save32 (VerificationEntry + 4, uint32_t (DeciderTag::NONE)) ; // i.e. undecided
  Initialise (MachineIndex, MachineSpec) ;
  memset (HistoryWorkspace, 0, (2 * SpaceLimit + 1) * TimeLimit) ;
  memset (PreviousConfig, 0, sizeof (int) * TimeLimit) ;
  memset (PreviousWorkspace, 0xFF, sizeof (int) * 5 * (2 * SpaceLimit + 1)) ;

  // We only check for matches when the tape head has just moved right, then left. This will occur
  // in every Cycler, and it reduces the checks (and the workspace) by 75%. So remember the last
  // two tape head positions:
  int TapeHeadMinus1 = 99, TapeHeadMinus2 = 99 ;

  while (StepCount < TimeLimit)
    {
    if (TapeHead == TapeHeadMinus2 && TapeHead + 1 == TapeHeadMinus1)
      {
      int prev = Previous[State][TapeHead] ;
      PreviousConfig[StepCount] = prev ;
      Previous[State][TapeHead] = StepCount ;
      while (prev != -1)
        {
        if (!memcmp (Tape + Leftmost, TapeHistory[prev] + Leftmost, Rightmost - Leftmost + 1))
          {
          Save32 (VerificationEntry, MachineIndex) ;
          Save32 (VerificationEntry + 4, uint32_t (DeciderTag::CYCLER)) ;
          Save32 (VerificationEntry + 8, VERIF_INFO_LENGTH) ;
  
          // Leftmost
          // Rightmost
          // State
          // TapeHead
          // InititialStepCount
          // FinalStepCount
          Save32 (VerificationEntry + 12, Leftmost) ;
          Save32 (VerificationEntry + 16, Rightmost) ;
          Save32 (VerificationEntry + 20, State) ;
          Save32 (VerificationEntry + 24, TapeHead) ;
          Save32 (VerificationEntry + 28, prev) ;
          Save32 (VerificationEntry + 32, StepCount) ;
          return ;
          }
        prev = PreviousConfig[prev] ;
        }

      memcpy (TapeHistory[StepCount] + Leftmost, Tape + Leftmost, Rightmost - Leftmost + 1) ;
      }
    TapeHeadMinus2 = TapeHeadMinus1 ;
    TapeHeadMinus1 = TapeHead ;

    switch (Step())
      {
      case StepResult::OK: break ;
      case StepResult::HALT: printf ("Unexpected HALT state reached!\n") ; exit (1) ;
      case StepResult::OUT_OF_BOUNDS: return ;
      }
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
      case 'D':
        if (argv[0][2] == 0) printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
        SeedDatabaseFile = std::string (&argv[0][2]) ;
        break ;

      case 'V':
        if (argv[0][2] == 0) printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
        VerificationFile = std::string (&argv[0][2]) ;
        break ;

      case 'U':
        if (argv[0][2] == 0) printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
        UndecidedFile = std::string (&argv[0][2]) ;
        break ;

      case 'T':
        TimeLimit = atoi (&argv[0][2]) ;
        TimeLimitPresent = true ;
        break ;

      case 'S':
        SpaceLimit = atoi (&argv[0][2]) ;
        SpaceLimitPresent = true ;
        break ;

      case 'M':
        CommandLineParams::nThreads = atoi (&argv[0][2]) ;
        CommandLineParams::nThreadsPresent = true ;
        break ;

      default:
        printf ("Invalid parameter \"%s\"\n", argv[0]) ;
        PrintHelpAndExit (1) ;
      }
    }

  if (VerificationFile.empty()) printf ("Verification file not specified\n"), PrintHelpAndExit (1) ;
  if (UndecidedFile.empty()) printf ("Undecided file not specified\n"), PrintHelpAndExit (1) ;

  if (!TimeLimitPresent) printf ("Time limit not specified\n"), PrintHelpAndExit (1) ;
  if (!SpaceLimitPresent) printf ("Space limit not specified\n"), PrintHelpAndExit (1) ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf (R"*RAW*(
Cyclers <param> <param>...
  <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
           -V<verification file>  Output file: verification data for decided machines
           -U<undecided file>     Output file: remaining undecided machines
           -T<time limit>         Max no. of steps
           -S<space limit>        Max absolute value of tape head
           -M<threads>            Number of threads to use
)*RAW*") ;
  exit (status) ;
  }
