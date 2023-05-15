// DecideBouncers  <param> <param>...
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
//            -B[<bells-file>]      Output <bells-file>.txt and <bells-file>.umf (default ProbableBells)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <string>
#include <vector>
#include <thread>

#include "BouncerDecider.h"
#include "../Params.h"

// Number of machines to assign to each thread
#define DEFAULT_CHUNK_SIZE 256
static uint32_t ChunkSize = DEFAULT_CHUNK_SIZE ;

#define VERIF_AVERAGE_LENGTH 10000 // Max average length of verification entries in a chunk

class CommandLineParams : public DeciderParams
  {
public:
  std::string BellsFile ;
  uint32_t TimeLimit ;  bool TimeLimitPresent = false ;
  uint32_t SpaceLimit ; bool SpaceLimitPresent = false ;
  bool OutputBells = false ;
  FILE* fpBellTxt = 0 ;
  FILE* fpBellUmf = 0 ;

  void Parse (int argc, char** argv) ;
  void PrintHelpAndExit [[noreturn]] (int status) ;

  virtual void OpenFiles() override
    {
    DeciderParams::OpenFiles() ;
    if (OutputBells)
      {
      if (BellsFile.empty()) BellsFile = "ProbableBells" ;
      fpBellTxt = OpenFile ((BellsFile + ".txt").c_str(), "wt") ;
      fpBellUmf = OpenFile ((BellsFile + ".umf").c_str(), "wb") ;
      }
    }
  } ;

static CommandLineParams Params ;
static TuringMachineReader Reader ;

int main (int argc, char** argv)
  {
  Params.Parse (argc, argv) ;
  Params.CheckParameters() ;
  Params.OpenFiles() ;

  Reader.SetParams (&Params) ;

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

  // Make sure the progress indicator updates reasonably often
  if (Params.nThreads * ChunkSize * 50 > Reader.nMachines)
    ChunkSize = 1 + Reader.nMachines / (50 * Params.nThreads) ;

  // Write dummy dvf header
  Write32 (Params.fpVerify, 0) ;

  clock_t Timer = clock() ;

  BouncerDecider** DeciderArray = new BouncerDecider*[Params.nThreads] ;
  uint32_t** MachineIndexList = new uint32_t*[Params.nThreads] ;
  uint8_t** MachineSpecList = new uint8_t*[Params.nThreads] ;
  uint8_t** VerificationEntryList = new uint8_t*[Params.nThreads] ;
  uint32_t* ChunkSizeArray = new uint32_t[Params.nThreads] ;
  for (uint32_t i = 0 ; i < Params.nThreads ; i++)
    {
    DeciderArray[i] = new BouncerDecider (Params.MachineStates, Params.TimeLimit,
      Params.SpaceLimit, Params.TraceOutput) ;
    DeciderArray[i] -> Clone = new BouncerDecider (Params.MachineStates, Params.TimeLimit,
      Params.SpaceLimit, Params.TraceOutput) ;
    MachineIndexList[i] = new uint32_t[ChunkSize] ;
    MachineSpecList[i] = new uint8_t[Reader.MachineSpecSize * ChunkSize] ;
    VerificationEntryList[i] = new uint8_t[VERIF_AVERAGE_LENGTH * DEFAULT_CHUNK_SIZE] ;
    }

  uint32_t nDecided = 0 ;
  uint32_t nCompleted = 0 ;
  uint32_t nProbableBells = 0 ;
  int LastPercent = -1 ;
  uint32_t nTimeLimitedDecided = 0 ;
  uint32_t nSpaceLimitedDecided = 0 ;

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

    std::vector<std::thread*> ThreadList (Params.nThreads) ;
    for (uint32_t i = 0 ; i < Params.nThreads ; i++)
      {
      for (uint32_t j = 0 ; j < ChunkSizeArray[i] ; j++)
        MachineIndexList[i][j] = Reader.Next (MachineSpecList[i] + j * Reader.MachineSpecSize) ;

      // Run inline if single thread (for ease of debugging)
      if (Params.nThreads == 1) DeciderArray[0] -> ThreadFunction (ChunkSizeArray[0],
        MachineIndexList[0], MachineSpecList[0], Reader.MachineSpecSize,
          VerificationEntryList[0], VERIF_AVERAGE_LENGTH * DEFAULT_CHUNK_SIZE) ;
      else ThreadList[i] = new std::thread (&BouncerDecider::ThreadFunction, DeciderArray[i],
        ChunkSizeArray[i], MachineIndexList[i], MachineSpecList[i], Reader.MachineSpecSize,
          VerificationEntryList[i], VERIF_AVERAGE_LENGTH * DEFAULT_CHUNK_SIZE) ;

      nCompleted += ChunkSizeArray[i] ;
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
      for (uint32_t j = 0 ; j < ChunkSizeArray[i] ; j++)
        {
        switch ((int)Load32 (VerificationEntry))
          {
          case -1:
            Write32 (Params.fpUndecided, MachineIndexList[i][j]) ;
            Write32 (Params.fpBellUmf, MachineIndexList[i][j]) ;
            if (Params.fpBellTxt) fprintf (Params.fpBellTxt, "%d\n", MachineIndexList[i][j]) ;
            nProbableBells++ ;
            VerificationEntry += 4 ;
            break ;

          case -2:
            Write32 (Params.fpUndecided, MachineIndexList[i][j]) ;
            VerificationEntry += 4 ;
            break ;

          default:
            {
            uint32_t InfoLength = Load32 (VerificationEntry + 8) ;
            Write (Params.fpVerify, VerificationEntry, VERIF_HEADER_LENGTH + InfoLength) ;
            nDecided++ ;
            if (MachineIndexList[i][j] < Reader.nTimeLimited) nTimeLimitedDecided++ ;
            else nSpaceLimitedDecided++ ;
            VerificationEntry += VERIF_HEADER_LENGTH + InfoLength ;
            }
            break ;
          }
        MachineSpec += Reader.MachineSpecSize ;
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
  if (Params.fpInput) fclose (Params.fpInput) ;

  Timer = clock() - Timer ;

  if (Params.fpVerify)
    {
    // Write the verification file header
    if (fseek (Params.fpVerify, 0 , SEEK_SET))
      printf ("\nfseek failed\n"), exit (1) ;
    Write32 (Params.fpVerify, nDecided) ;
    fclose (Params.fpVerify) ;
    }

  if (Params.fpBellUmf) fclose (Params.fpBellUmf) ;
  if (Params.fpBellTxt) fclose (Params.fpBellTxt) ;

  printf ("\nDecided %d out of %d\n", nDecided, Reader.nMachines) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;

  uint32_t nUnilateral = 0 ;
  uint32_t nBilateral = 0 ;
  uint32_t nTranslated = 0 ;
  uint32_t nDouble = 0 ;
  uint32_t nMultiple = 0 ;
  uint32_t nPartitioned = 0 ;
  uint32_t nHalters = 0 ;

  uint32_t nRunsMax = 0 ;
  uint32_t nRunsMachine = 0 ;
  uint32_t MaxRepeaterPeriod = 0 ;
  uint32_t MaxRepeaterMachine = 0 ;
  int MinStat = INT_MAX ;
  uint32_t MinStatMachine = 0 ;
  int MaxStat = INT_MIN ;
  uint32_t MaxStatMachine = 0 ;

  for (uint32_t i = 0 ; i < Params.nThreads ; i++)
    {
    nUnilateral  += DeciderArray[i] -> nUnilateral ;
    nBilateral   += DeciderArray[i] -> nBilateral ;
    nTranslated  += DeciderArray[i] -> nTranslated ;
    nDouble      += DeciderArray[i] -> nDouble ;
    nMultiple    += DeciderArray[i] -> nMultiple ;
    nPartitioned += DeciderArray[i] -> nPartitioned ;
    nHalters     += DeciderArray[i] -> nHalters ;

    if (DeciderArray[i] -> nRunsMax > nRunsMax)
      {
      nRunsMax = DeciderArray[i] -> nRunsMax ;
      nRunsMachine = DeciderArray[i] -> nRunsMachine ;
      }
    if (DeciderArray[i] -> MaxRepeaterPeriod > MaxRepeaterPeriod)
      {
      MaxRepeaterPeriod = DeciderArray[i] -> MaxRepeaterPeriod ;
      MaxRepeaterMachine = DeciderArray[i] -> MaxRepeaterMachine ;
      }
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
  printf ("%d Unilateral\n", nUnilateral) ;
  printf ("%d Bilateral\n", nBilateral) ;
  printf ("%d Translated\n", nTranslated) ;
  if (nHalters)
    printf ("%d Halter%s\n", nHalters, nHalters == 1 ? "" : "s") ;

  printf ("\n%d Double\n", nDouble) ;
  printf ("%d Multiple\n", nMultiple) ;
  printf ("%d Partitioned\n", nPartitioned) ;
  printf ("%d Probable Bells\n", nProbableBells) ;
  printf ("\n%d: %d runs\n", nRunsMachine, nRunsMax) ;
  printf ("%d: RepeaterPeriod %d\n", MaxRepeaterMachine, MaxRepeaterPeriod) ;
  if (MinStat != INT_MAX) printf ("\n%d: MinStat = %d\n", MinStatMachine, MinStat) ;
  if (MaxStat != INT_MIN) printf ("\n%d: MaxStat = %d\n", MaxStatMachine, MaxStat) ;
  }

void BouncerDecider::ThreadFunction (int nMachines, const uint32_t* MachineIndexList,
  const uint8_t* MachineSpecList, uint32_t MachineSpecSize, uint8_t* VerificationEntryList, uint32_t VerifLength)
  {
  const uint8_t* VerificationEntryLimit = VerificationEntryList + VerifLength ;
  VerificationEntryLimit -= VERIF_INFO_MAX_LENGTH ;
  while (nMachines--)
    {
    SeedDatabaseIndex = *MachineIndexList++ ;
    Save32 (VerificationEntryList, SeedDatabaseIndex) ;
    Save32 (VerificationEntryList + 4, uint32_t (DeciderTag::NEW_BOUNCER)) ;
    if (RunDecider (MachineSpecList, VerificationEntryList))
      VerificationEntryList += VERIF_HEADER_LENGTH + Load32 (VerificationEntryList + 8) ;
    else
      {
      Save32 (VerificationEntryList, Type == BouncerType::Bell ? -1 : -2) ;
      VerificationEntryList += 4 ;
      }

    if (VerificationEntryList > VerificationEntryLimit)
      printf ("\nVerificationEntryLimit exceeded\n"), exit (1) ;

    MachineSpecList += MachineSpecSize ;
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

      case 'B':
        OutputBells = true ;
        if (argv[0][2]) BellsFile = std::string (&argv[0][2]) ;
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
  printf ("Bouncers <param> <param>...") ;
  DeciderParams::PrintHelp() ;
  printf (R"*RAW*(
           -T<time limit>        Max no. of steps
           -S<space limit>       Max absolute value of tape head
           -B[<bells-file>]      Output <bells-file>.txt and <bells-file>.umf (default ProbableBells)
)*RAW*") ;
  exit (status) ;
  }
