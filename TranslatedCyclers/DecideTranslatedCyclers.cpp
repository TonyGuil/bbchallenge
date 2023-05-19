// DecideTranslatedCyclers <param> <param>...
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
//            -S<space limit>       Max absolute value of tape head

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <string>
#include <vector>

#include "TranslatedCycler.h"
#include "../Params.h"

#if NEED_BOOST_THREADS
  #include <boost/thread.hpp>
  using boost::thread ;
#else
  #include <thread>
  using std::thread ;
#endif

#define CHUNK_SIZE 1024 // Number of machines to assign to each thread

class CommandLineParams : public DeciderParams
  {
public:
  void Parse (int argc, char** argv) ;
  virtual void PrintHelpAndExit [[noreturn]] (int status) ;
  uint32_t TimeLimit ;  bool TimeLimitPresent = false ;
  uint32_t SpaceLimit ; bool SpaceLimitPresent = false ;
  } ;
  
static CommandLineParams Params ;

int main (int argc, char** argv)
  {
  Params.Parse (argc, argv) ;
  Params.CheckParameters() ;
  Params.OpenFiles() ;

  TuringMachineReader Reader (&Params) ;

  // If we are using the 5-state seed database, then we know which machines
  // are time-limited and which are space-limited, so we can restrict the
  // search to space-limited machines:
  uint32_t nTimeLimited = 0 ;
  uint32_t nSpaceLimited = Reader.nMachines ;
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
  std::vector<thread*> ThreadList (Params.nThreads) ;

  clock_t Timer = clock() ;

  TranslatedCycler** DeciderArray = new TranslatedCycler*[Params.nThreads] ;
  uint32_t** MachineIndexList = new uint32_t*[Params.nThreads] ;
  uint8_t** MachineSpecList = new uint8_t*[Params.nThreads] ;
  uint8_t** VerificationEntryList = new uint8_t*[Params.nThreads] ;
  uint32_t* ChunkSize = new uint32_t[Params.nThreads] ;

  // Allocate the per-thread workspace
  for (uint32_t i = 0 ; i < Params.nThreads ; i++)
    {
    DeciderArray[i] = new TranslatedCycler (Params.MachineStates, Params.TimeLimit,
      Params.SpaceLimit, Params.TraceOutput) ;
    DeciderArray[i] -> Clone = new TranslatedCycler (Params.MachineStates, Params.TimeLimit,
      Params.SpaceLimit, Params.TraceOutput) ;
    MachineIndexList[i] = new uint32_t[CHUNK_SIZE] ;
    MachineSpecList[i] = new uint8_t[Reader.MachineSpecSize * CHUNK_SIZE] ;
    VerificationEntryList[i] = new uint8_t[VERIF_ENTRY_LENGTH * CHUNK_SIZE] ;
    }

  uint32_t nDecided = 0 ;
  uint32_t nTimeLimitedComplete = 0 ;
  uint32_t nSpaceLimitedComplete = 0 ;
  int LastPercent = -1 ;

  if (Params.MachineLimitPresent && nSpaceLimited > Params.MachineLimit)
    nSpaceLimited = Params.MachineLimit ;
  uint32_t MachineCounter = 0 ;
  while (nSpaceLimitedComplete < nSpaceLimited)
    {
    uint32_t nRemaining = nSpaceLimited - nSpaceLimitedComplete ;
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

    std::vector<thread*> ThreadList (Params.nThreads) ;
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
          if (Reader.OrigSeedDatabase) while (MachineIndex < Reader.nTimeLimited)
            {
            Write32 (Params.fpUndecided, MachineIndex) ;
            nTimeLimitedComplete++ ;
            MachineIndex = Params.fpInput ? Read32 (Params.fpInput) : MachineCounter++ ;
            }
          MachineIndexList[i][j] = MachineIndex ;
          Reader.Read (MachineIndex, MachineSpecList[i] + j * Reader.MachineSpecSize) ;
          }
        }
      nSpaceLimitedComplete += ChunkSize[i] ;

      // Run inline if single thread (for ease of debugging)
      if (Params.nThreads == 1) DeciderArray[i] -> ThreadFunction (ChunkSize[i],
        MachineIndexList[i], MachineSpecList[i], VerificationEntryList[i]) ;
      else ThreadList[i] = new thread (&TranslatedCycler::ThreadFunction,
        DeciderArray[i], ChunkSize[i], MachineIndexList[i], MachineSpecList[i], VerificationEntryList[i]) ;
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
          if (Params.fpVerify && fwrite (VerificationEntry, VERIF_ENTRY_LENGTH, 1, Params.fpVerify) != 1)
            printf ("Error writing file\n"), exit (1) ;
          nDecided++ ;
          }
        else Write32 (Params.fpUndecided, MachineIndexList[i][j]) ;
        MachineSpec += Reader.MachineSpecSize ;
        VerificationEntry += VERIF_ENTRY_LENGTH ;
        }
      }

    int Percent = (nSpaceLimitedComplete * 100LL) / nSpaceLimited ;
    if (Percent != LastPercent)
      {
      LastPercent = Percent ;
      printf ("\r%d%% %d %d", Percent, nSpaceLimitedComplete, nDecided) ;
      fflush (stdout) ;
      }
    }

  // Just in case the input file was not sorted, check for stragglers
  if (Params.fpUndecided)
    {
    while (nTimeLimitedComplete < nTimeLimited)
      {
      uint32_t MachineIndex = Params.fpInput ? Read32 (Params.fpInput) : MachineCounter++ ;
      Write32 (Params.fpUndecided, MachineIndex) ;
      }
    fclose (Params.fpUndecided) ;
    }

  printf ("\n") ;

  // Check that we've reached the end of the input file
  if (!Params.MachineLimitPresent && Params.fpInput &&
    fread (&nTimeLimitedComplete, 1, 1, Params.fpInput) != 0) // Use nTimeLimitedComplete as buff
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
    printf ("\nDecided %d out of %d space-limited machines\n", nDecided, nSpaceLimitedComplete) ;
  else printf ("\nDecided %d out of %d\n", nDecided, nSpaceLimitedComplete) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;

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
  if (MinStat != INT_MAX)
    {
    if (MaxStat != INT_MIN) printf ("\n%d (%d) <= Stat <= %d (%d)\n",
      MinStat, MinStatMachine, MaxStat, MaxStatMachine) ;
    else printf ("\n%d: MinStat = %d\n", MinStatMachine, MinStat) ;
    }
  else if (MaxStat != INT_MIN) printf ("\n%d: MaxStat = %d\n", MaxStatMachine, MaxStat) ;
  }

void TranslatedCycler::ThreadFunction (int nMachines, const uint32_t* MachineIndexList,
  const uint8_t* MachineSpecList, uint8_t* VerificationEntryList)
  {
  while (nMachines--)
    {
    SeedDatabaseIndex = *MachineIndexList++ ;
    Save32 (VerificationEntryList + 4, uint32_t (DeciderTag::NONE)) ;
    Run (MachineSpecList, VerificationEntryList) ;

    MachineSpecList += MachineSpecSize ;
    VerificationEntryList += VERIF_ENTRY_LENGTH ;
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
  if (!SpaceLimitPresent) SpaceLimit = 100000 ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf ("TranslatedCyclers <param> <param>...") ;
  DeciderParams::PrintHelp() ;
  printf (R"*RAW*(
           -T<time limit>        Max no. of steps
           -S<space limit>       Max absolute value of tape head
)*RAW*") ;
  exit (status) ;
  }
