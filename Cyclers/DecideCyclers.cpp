// DecideCyclers <param> <param>...
//   <param>: -N<states>            Machine states (2, 3, 4, 5, or 6)
//            -D<database>          Seed database file (defaults to ../SeedDatabase.bin)
//            -V<verification file> Output file: verification data for decided machines
//            -I<input file>        Input file: list of machines to be analysed (default=all machines)
//            -U<undecided file>    Output file: remaining undecided machines
//            -X<test machine>      Machine to test
//            -M<machine spec>      Compact machine code (ASCII spec) to test
//            -L<machine limit>     Max no. of machines to test
//            -H<threads>           Number of threads to use
//            -O                    Print trace output
//            -T<time limit>        Max no. of steps
//            -S<space limit>       Max absolute value of tape head)*RAW*") ;

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <vector>
#include <thread>

#include "../TuringMachine.h"
#include "../Params.h"

#define CHUNK_SIZE 1024 // Number of machines to assign to each thread

#define VERIF_INFO_LENGTH 24 // Length of DeciderSpecificInfo in Verification File

class CommandLineParams : public DeciderParams
  {
public:
  uint32_t TimeLimit ;     bool TimeLimitPresent = false ;
  uint32_t SpaceLimit ;    bool SpaceLimitPresent = false ;
  void Parse (int argc, char** argv) ;
  void PrintHelpAndExit [[noreturn]] (int status) ;
  } ;

static CommandLineParams Params ;
static TuringMachineReader Reader ;

class Cycler : public TuringMachine
  {
public:
  Cycler (uint32_t  MachineStates, uint32_t TimeLimit, uint32_t SpaceLimit)
  : TuringMachine (MachineStates, SpaceLimit)
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
    PreviousWorkspace = new int[MachineStates * (2 * SpaceLimit + 1)] ;
    Previous[0] = 0 ;
    Previous[1] = PreviousWorkspace + SpaceLimit ;
    for (uint32_t i = 2 ; i <= MachineStates ; i++)
      Previous[i] = Previous[i - 1] + 2 * SpaceLimit + 1 ;
    }

  void ThreadFunction (int nMachines, const uint32_t* MachineIndexList,
    const uint8_t* MachineSpecList, uint8_t* VerificationEntryList) ;

private:

  void Run (uint32_t MachineIndex, const uint8_t* MachineSpec, uint8_t* VerificationEntry) ;

  uint32_t TimeLimit ;

  uint8_t* HistoryWorkspace ;
  uint8_t** TapeHistory ;

  int* PreviousConfig ;
  int* PreviousWorkspace ;
  int* Previous[MAX_MACHINE_STATES + 1] ;
  } ;

int main (int argc, char** argv)
  {
  Params.Parse (argc, argv) ;
  Params.CheckParameters() ;
  Params.OpenFiles() ;

  Reader.SetParams (&Params) ;

  // If we are using the 5-state seed database, then we know which machines
  // are time-limited and which are space-limited, so we can restrict the
  // search to time-limited machines:
  uint32_t nTimeLimited = Reader.nMachines ;
  uint32_t nSpaceLimited = 0 ;
  if (Reader.OrigSeedDatabase)
    {
    if (Params.fpInput == 0)
      {
      nTimeLimited = Reader.nTimeLimited ;
      nSpaceLimited = Reader.nSpaceLimited ;
      }
    else
      {
      // Count the space-limited machines in the input file (this is just
      // so we can give informative percentages in the progress report)
      nSpaceLimited = 0 ;
      for (uint32_t i = 0 ; i < Reader.nMachines ; i++)
        {
        if (Read32 (Params.fpInput) < Reader.nTimeLimited) nTimeLimited++ ;
        else nSpaceLimited++ ;
        }
      if (fseek (Params.fpInput, 0, SEEK_SET))
        printf ("fseek failed\n"), exit (1) ;
      }
    }

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

  Cycler** CyclerArray = new Cycler*[Params.nThreads] ;
  uint32_t** MachineIndexList = new uint32_t*[Params.nThreads] ;
  uint8_t** MachineSpecList = new uint8_t*[Params.nThreads] ;
  uint8_t** VerificationEntryList = new uint8_t*[Params.nThreads] ;
  uint32_t* ChunkSize = new uint32_t[Params.nThreads] ;

  // Allocate the per-thread workspace
  for (uint32_t i = 0 ; i < Params.nThreads ; i++)
    {
    CyclerArray[i] = new Cycler (Params.MachineStates, Params.TimeLimit, Params.SpaceLimit) ;
    MachineIndexList[i] = new uint32_t[CHUNK_SIZE] ;
    MachineSpecList[i] = new uint8_t[Reader.MachineSpecSize * CHUNK_SIZE] ;
    VerificationEntryList[i] = new uint8_t[VERIF_ENTRY_LENGTH * CHUNK_SIZE] ;
    }

  uint32_t nDecided = 0 ;
  uint32_t nTimeLimitedComplete = 0 ;
  uint32_t nSpaceLimitedComplete = 0 ;
  int LastPercent = -1 ;

  // Analyse the time-limited machines in the seed database
  if (Params.MachineLimitPresent && nTimeLimited > Params.MachineLimit)
    nTimeLimited = Params.MachineLimit ;
  uint32_t MachineCounter = 0 ;
  while (nTimeLimitedComplete < nTimeLimited)
    {
    uint32_t nRemaining = nTimeLimited - nTimeLimitedComplete ;
    if (nRemaining >= Params.nThreads * CHUNK_SIZE)
      {
      for (uint32_t i = 0 ; i < Params.nThreads ; i++) ChunkSize[i] = CHUNK_SIZE ;
      }
    else
      {
      for (uint32_t i = 0 ; i < Params.nThreads ; i++)
        {
        ChunkSize[i] = nRemaining / (Params.nThreads - i) ;
        nRemaining -= ChunkSize[i] ;
        }
      }

    std::vector<std::thread*> ThreadList (Params.nThreads) ;
    for (uint32_t i = 0 ; i < Params.nThreads ; i++)
      {
      for (uint32_t j = 0 ; j < ChunkSize[i] ; j++)
        {
        if (Reader.SingleEntry)
          {
          if (Params.MachineSpec.empty()) MachineIndexList[i][0] = Params.TestMachine ;
          else MachineIndexList[i][0] = 0 ;
          Reader.Next (MachineSpecList[i]) ;
          }
        else
          {
          uint32_t MachineIndex = Params.fpInput ? Read32 (Params.fpInput) : MachineCounter++ ;
          if (Reader.OrigSeedDatabase) while (MachineIndex >= Reader.nTimeLimited)
            {
            Write32 (Params.fpUndecided, MachineIndex) ;
            nSpaceLimitedComplete++ ;
            MachineIndex = Params.fpInput ? Read32 (Params.fpInput) : MachineCounter++ ;
            }
          MachineIndexList[i][j] = MachineIndex ;
          Reader.Read (MachineIndex, MachineSpecList[i] + j * Reader.MachineSpecSize) ;
          }
        }
      nTimeLimitedComplete += ChunkSize[i] ;

      // Run inline if single thread (for ease of debugging)
      if (Params.nThreads == 1) CyclerArray[i] -> ThreadFunction (ChunkSize[i],
        MachineIndexList[i], MachineSpecList[i], VerificationEntryList[i]) ;
      else ThreadList[i] = new std::thread (&Cycler::ThreadFunction,
        CyclerArray[i], ChunkSize[i], MachineIndexList[i], MachineSpecList[i], VerificationEntryList[i]) ;
      }

    for (uint32_t i = 0 ; i < Params.nThreads ; i++)
      {
      if (Params.nThreads != 1)
        {
        ThreadList[i] -> join() ; // Wait for thread i to finish
        delete ThreadList[i] ;
        }

      const uint8_t* MachineSpec = MachineSpecList[i] ;
      const uint8_t* VerificationEntry = VerificationEntryList[i] ;
      for (uint32_t j = 0 ; j < ChunkSize[i] ; j++)
        {
        if (Load32 (VerificationEntry + 4))
          {
          Write (Params.fpVerify, VerificationEntry, VERIF_ENTRY_LENGTH) ;
          nDecided++ ;
          }
        else Write32 (Params.fpUndecided, MachineIndexList[i][j]) ;
        MachineSpec += Reader.MachineSpecSize ;
        VerificationEntry += VERIF_ENTRY_LENGTH ;
        }
      }

    int Percent = (nTimeLimitedComplete * 100LL) / nTimeLimited ;
    if (Percent != LastPercent)
      {
      LastPercent = Percent ;
      printf ("\r%d%% %d %d", Percent, nTimeLimitedComplete, nDecided) ;
      fflush (stdout) ;
      }
    }

  if (Params.fpUndecided)
    {
    while (nSpaceLimitedComplete++ < nSpaceLimited)
      {
      uint32_t MachineIndex = Params.fpInput ? Read32 (Params.fpInput) : MachineCounter++ ;
      Write32 (Params.fpUndecided, MachineIndex) ;
      }
    fclose (Params.fpUndecided) ;
    }

  printf ("\n") ;

  // Check that we've reached the end of the input file
  if (!Params.MachineLimitPresent && Params.fpInput && !CheckEndOfFile (Params.fpInput))
    printf ("\nInput file too long!\n"), exit (1) ;

  if (Params.fpVerify)
    {
    // Write the verification file header
    if (fseek (Params.fpVerify, 0 , SEEK_SET))
      printf ("\nfseek failed\n"), exit (1) ;
    Write32 (Params.fpVerify, nDecided) ;
    fclose (Params.fpVerify) ;
    }

  Timer = clock() - Timer ;

  if (Params.fpInput) fclose (Params.fpInput) ;


  if (Reader.OrigSeedDatabase)
    printf ("\nDecided %d out of %d time-limited machines\n", nDecided, Reader.nTimeLimited) ;
  else printf ("\nDecided %d out of %d\n", nDecided, Reader.nMachines) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;
  }

void Cycler::ThreadFunction (int nMachines, const uint32_t* MachineIndexList,
  const uint8_t* MachineSpecList, uint8_t* VerificationEntryList)
  {
  while (nMachines--)
    {
    Save32 (VerificationEntryList + 4, uint32_t (DeciderTag::NONE)) ;
    Run (*MachineIndexList++, MachineSpecList, VerificationEntryList) ;
    MachineSpecList += MachineSpecSize ;
    VerificationEntryList += VERIF_ENTRY_LENGTH ;
    }
  }

void Cycler::Run (uint32_t MachineIndex, const uint8_t* MachineSpec, uint8_t* VerificationEntry)
  {
  Save32 (VerificationEntry + 4, uint32_t (DeciderTag::NONE)) ; // i.e. undecided
  Initialise (MachineIndex, MachineSpec) ;
  memset (HistoryWorkspace, 0, (2 * SpaceLimit + 1) * TimeLimit) ;
  memset (PreviousConfig, 0, sizeof (int) * TimeLimit) ;
  memset (PreviousWorkspace, 0xFF, sizeof (int) * MachineStates * (2 * SpaceLimit + 1)) ;

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
      case StepResult::HALT: return ; // The BouncerDecider knows what to do with these
      case StepResult::OUT_OF_BOUNDS: return ;
      }
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
      case 'T':
        TimeLimit = atoi (&argv[0][2]) ;
        TimeLimitPresent = true ;
        break ;

      case 'S':
        SpaceLimit = atoi (&argv[0][2]) ;
        SpaceLimitPresent = true ;
        break ;

      default:
        printf ("Invalid parameter \"%s\"\n", argv[0]) ;
        PrintHelpAndExit (1) ;
      }
    }

  if (!TimeLimitPresent) printf ("Time limit not specified\n"), PrintHelpAndExit (1) ;
  if (!SpaceLimitPresent) printf ("Space limit not specified\n"), PrintHelpAndExit (1) ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf ("DecideCyclers <param> <param>...") ;
  DeciderParams::PrintHelp() ;
  printf (R"*RAW*(
           -T<time limit>        Max no. of steps
           -S<space limit>       Max absolute value of tape head)*RAW*") ;
  exit (status) ;
  }
