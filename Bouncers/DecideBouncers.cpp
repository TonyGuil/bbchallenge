#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <string>
#include <boost/thread.hpp>

#include "BouncerDecider.h"

// Number of machines to assign to each thread
#define DEFAULT_CHUNK_SIZE 256
static uint32_t ChunkSize = DEFAULT_CHUNK_SIZE ;

#define VERIF_AVERAGE_LENGTH 10000 // Max average length of verification entries in a chunk

class CommandLineParams
  {
public:
  static std::string SeedDatabaseFile ;
  static std::string InputFile ;
  static std::string VerificationFile ;
  static std::string UndecidedFile ;
  static std::string BellsFile ;
  static uint32_t TimeLimit ;     static bool TimeLimitPresent ;
  static uint32_t SpaceLimit ;    static bool SpaceLimitPresent ;
  static uint32_t nThreads ;      static bool nThreadsPresent ;
  static uint32_t TestMachine ;   static bool TestMachinePresent ;
  static uint32_t MachineLimit ;  static bool MachineLimitPresent ;
  static bool OutputBells ;
  static bool TraceOutput ;
  static void Parse (int argc, char** argv) ;
  static void PrintHelpAndExit [[noreturn]] (int status) ;
  } ;

std::string CommandLineParams::SeedDatabaseFile ;
std::string CommandLineParams::InputFile ;
std::string CommandLineParams::VerificationFile ;
std::string CommandLineParams::UndecidedFile ;
std::string CommandLineParams::BellsFile ;
uint32_t CommandLineParams::TimeLimit ;     bool CommandLineParams::TimeLimitPresent ;
uint32_t CommandLineParams::SpaceLimit ;    bool CommandLineParams::SpaceLimitPresent ;
uint32_t CommandLineParams::nThreads ;      bool CommandLineParams::nThreadsPresent ;
uint32_t CommandLineParams::TestMachine ;   bool CommandLineParams::TestMachinePresent ;
uint32_t CommandLineParams::MachineLimit ;  bool CommandLineParams::MachineLimitPresent ;
bool CommandLineParams::OutputBells ;
bool CommandLineParams::TraceOutput ;

int main (int argc, char** argv)
  {
  CommandLineParams::Parse (argc, argv) ;

  TuringMachineReader Reader (CommandLineParams::SeedDatabaseFile.c_str()) ;

  FILE* fpVerif = 0 ;
  if (!CommandLineParams::VerificationFile.empty())
    {
    fpVerif = fopen (CommandLineParams::VerificationFile.c_str(), "wb") ;
    if (fpVerif == NULL)
      printf ("Can't open output file \"%s\"\n", CommandLineParams::VerificationFile.c_str()), exit (1) ;
    }

  uint8_t MachineSpec[MACHINE_SPEC_SIZE] ;
  if (CommandLineParams::TestMachinePresent)
    {
    BouncerDecider Decider (CommandLineParams::TimeLimit,
      CommandLineParams::SpaceLimit, CommandLineParams::TraceOutput) ;
    Decider.Clone = new BouncerDecider (CommandLineParams::TimeLimit,
      CommandLineParams::SpaceLimit, CommandLineParams::TraceOutput) ;
    Decider.SeedDatabaseIndex = CommandLineParams::TestMachine ;
    Reader.Read (Decider.SeedDatabaseIndex, MachineSpec) ;
    uint8_t* VerificationEntry = 0 ;
    if (fpVerif) VerificationEntry = new uint8_t[VERIF_INFO_MAX_LENGTH] ;
    bool Decided = Decider.RunDecider (MachineSpec, VerificationEntry) ;
    printf ("%d\n", Decided) ;
    if (Decided && fpVerif)
      {
      Save32 (VerificationEntry, Decider.SeedDatabaseIndex) ;
      Save32 (VerificationEntry + 4, (int)DeciderTag::BOUNCER) ;
      Write32 (fpVerif, 1) ;
      uint32_t InfoLength = Load32 (VerificationEntry + 8) ;
      if (fpVerif && fwrite (VerificationEntry,
        VERIF_HEADER_LENGTH + InfoLength, 1, fpVerif) != 1)
          printf ("Error writing file\n"), exit (1) ;
      }
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

  FILE* fpUndecided = 0 ;
  if (!CommandLineParams::UndecidedFile.empty())
    {
    fpUndecided = fopen (CommandLineParams::UndecidedFile.c_str(), "wb") ;
    if (fpUndecided == NULL)
      printf ("Can't open output file \"%s\"\n", CommandLineParams::UndecidedFile.c_str()), exit (1) ;
    }

  FILE* fpBellTxt = 0 ;
  FILE* fpBellUmf = 0 ;
  if (CommandLineParams::OutputBells)
    {
    if (CommandLineParams::BellsFile.empty())
      CommandLineParams::BellsFile = std::string ("ProbableBells") ;
    fpBellTxt = fopen ((CommandLineParams::BellsFile + ".txt").c_str(), "wt") ;
    if (fpBellTxt == 0) printf ("Can't open %s.txt for writing\n",
      CommandLineParams::BellsFile.c_str()), exit (1) ;
    fpBellUmf = fopen ((CommandLineParams::BellsFile + ".umf").c_str(), "wb") ;
    if (fpBellTxt == 0) printf ("Can't open %s.umf for writing\n",
      CommandLineParams::BellsFile.c_str()), exit (1) ;
    }

  uint32_t nMachines = InputFileSize >> 2 ;
  uint32_t nProbableBells = 0 ;

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

  // Make sure the progress indicator updates reasonably often
  if (CommandLineParams::nThreads * ChunkSize * 50 > nMachines)
    ChunkSize = 1 + nMachines / (50 * CommandLineParams::nThreads) ;

  // Write dummy dvf header
  Write32 (fpVerif, 0) ;

  clock_t Timer = clock() ;

  BouncerDecider** DeciderArray = new BouncerDecider*[CommandLineParams::nThreads] ;
  uint32_t** MachineIndexList = new uint32_t*[CommandLineParams::nThreads] ;
  uint8_t** MachineSpecList = new uint8_t*[CommandLineParams::nThreads] ;
  uint8_t** VerificationEntryList = new uint8_t*[CommandLineParams::nThreads] ;
  uint32_t* ChunkSizeArray = new uint32_t[CommandLineParams::nThreads] ;
  for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
    {
    DeciderArray[i] = new BouncerDecider (CommandLineParams::TimeLimit,
      CommandLineParams::SpaceLimit, CommandLineParams::TraceOutput) ;
    DeciderArray[i] -> Clone = new BouncerDecider (CommandLineParams::TimeLimit,
      CommandLineParams::SpaceLimit, CommandLineParams::TraceOutput) ;
    MachineIndexList[i] = new uint32_t[ChunkSize] ;
    MachineSpecList[i] = new uint8_t[MACHINE_SPEC_SIZE * ChunkSize] ;
    VerificationEntryList[i] = new uint8_t[VERIF_AVERAGE_LENGTH * DEFAULT_CHUNK_SIZE] ;
    }

  uint32_t nDecided = 0 ;
  uint32_t nCompleted = 0 ;
  int LastPercent = -1 ;
  uint32_t nTimeLimitedDecided = 0 ;
  uint32_t nSpaceLimitedDecided = 0 ;

  if (CommandLineParams::MachineLimitPresent) nMachines = CommandLineParams::MachineLimit ;
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

      ThreadList[i] = new boost::thread (BouncerDecider::ThreadFunction, DeciderArray[i],
        ChunkSizeArray[i], MachineIndexList[i], MachineSpecList[i],
          VerificationEntryList[i], VERIF_AVERAGE_LENGTH * DEFAULT_CHUNK_SIZE) ;
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
        switch ((int)Load32 (VerificationEntry))
          {
          case -1:
            Write32 (fpUndecided, MachineIndexList[i][j]) ;
            Write32 (fpBellUmf, MachineIndexList[i][j]) ;
            if (fpBellTxt) fprintf (fpBellTxt, "%d\n", MachineIndexList[i][j]) ;
            nProbableBells++ ;
            VerificationEntry += 4 ;
            break ;

          case -2:
            Write32 (fpUndecided, MachineIndexList[i][j]) ;
            VerificationEntry += 4 ;
            break ;

          default:
            {
            uint32_t InfoLength = Load32 (VerificationEntry + 8) ;
            if (fpVerif && fwrite (VerificationEntry,
              VERIF_HEADER_LENGTH + InfoLength, 1, fpVerif) != 1)
                printf ("Error writing file\n"), exit (1) ;
            nDecided++ ;
            if (MachineIndexList[i][j] < Reader.nTimeLimited) nTimeLimitedDecided++ ;
            else nSpaceLimitedDecided++ ;
            VerificationEntry += VERIF_HEADER_LENGTH + InfoLength ;
            }
            break ;
          }
        MachineSpec += MACHINE_SPEC_SIZE ;
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

  Timer = clock() - Timer ;

  if (fpVerif)
    {
    // Write the verification file header
    if (fseek (fpVerif, 0 , SEEK_SET))
      printf ("\nfseek failed\n"), exit (1) ;
    Write32 (fpVerif, nDecided) ;
    fclose (fpVerif) ;
    }

  if (fpBellUmf) fclose (fpBellUmf) ;
  if (fpBellTxt) fclose (fpBellTxt) ;

  printf ("\nDecided %d out of %d\n", nDecided, nMachines) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;

  uint32_t nUnilateral = 0 ;
  uint32_t nBilateral = 0 ;
  uint32_t nTranslated = 0 ;
  uint32_t nDouble = 0 ;
  uint32_t nMultiple = 0 ;
  uint32_t nPartitioned = 0 ;

  uint32_t nRunsMax = 0 ;
  uint32_t nRunsMachine = 0 ;
  uint32_t MaxRepeaterPeriod = 0 ;
  uint32_t MaxRepeaterMachine = 0 ;
  int MinStat = INT_MAX ;
  uint32_t MinStatMachine = 0 ;
  int MaxStat = INT_MIN ;
  uint32_t MaxStatMachine = 0 ;

  for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
    {
    nUnilateral  += DeciderArray[i] -> nUnilateral ;
    nBilateral   += DeciderArray[i] -> nBilateral ;
    nTranslated  += DeciderArray[i] -> nTranslated ;
    nDouble      += DeciderArray[i] -> nDouble ;
    nMultiple    += DeciderArray[i] -> nMultiple ;
    nPartitioned += DeciderArray[i] -> nPartitioned ;

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
  const uint8_t* MachineSpecList, uint8_t* VerificationEntryList, uint32_t VerifLength)
  {
  const uint8_t* VerificationEntryLimit = VerificationEntryList + VerifLength ;
  VerificationEntryLimit -= VERIF_INFO_MAX_LENGTH ;
  while (nMachines--)
    {
    SeedDatabaseIndex = *MachineIndexList++ ;
    if (RunDecider (MachineSpecList, VerificationEntryList))
      {
      Save32 (VerificationEntryList, SeedDatabaseIndex) ;
      Save32 (VerificationEntryList + 4, uint32_t (DeciderTag::BOUNCER)) ;
      VerificationEntryList += VERIF_HEADER_LENGTH + Load32 (VerificationEntryList + 8) ;
      }
    else
      {
      Save32 (VerificationEntryList, Type == BouncerType::Bell ? -1 : -2) ;
      VerificationEntryList += 4 ;
      }

    if (VerificationEntryList > VerificationEntryLimit)
      printf ("\nVerificationEntryLimit exceeded\n"), exit (1) ;

    MachineSpecList += MACHINE_SPEC_SIZE ;
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

      case 'T':
        TimeLimit = atoi (&argv[0][2]) ;
        TimeLimitPresent = true ;
        break ;

      case 'S':
        SpaceLimit = atoi (&argv[0][2]) ;
        SpaceLimitPresent = true ;
        break ;

      case 'M':
        nThreads = atoi (&argv[0][2]) ;
        nThreadsPresent = true ;
        break ;

      case 'X':
        TestMachine = atoi (&argv[0][2]) ;
        TestMachinePresent = true ;
        break ;

      case 'L':
        MachineLimit = atoi (&argv[0][2]) ;
        MachineLimitPresent = true ;
        break ;

      case 'B':
        OutputBells = true ;
        if (argv[0][2]) BellsFile = std::string (&argv[0][2]) ;
        break ;

      case 'O':
        TraceOutput = true ;
        break ;

      default:
        printf ("Invalid parameter \"%s\"\n", argv[0]) ;
        PrintHelpAndExit (1) ;
      }
    }

  if (!TestMachinePresent && InputFile.empty())
    printf ("Input file not specified\n"), PrintHelpAndExit (1) ;

  if (!TimeLimitPresent) printf ("Time limit not specified\n"), PrintHelpAndExit (1) ;
  if (!SpaceLimitPresent) printf ("Space limit not specified\n"), PrintHelpAndExit (1) ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf (R"*RAW*(
Bouncers <param> <param>...
  <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
           -I<input file>         Input file: list of machines to be analysed
           -V<verification file>  Output file: verification data for decided machines
           -U<undecided file>     Output file: remaining undecided machines
           -T<time limit>         Max no. of steps
           -S<space limit>        Max absolute value of tape head
           -X<test machine>       Machine to test
           -M<threads>            Number of threads to use
           -L<machine limit>      Max no. of machines to test
           -B[<bells-file>]       Output <bells-file>.txt and <bells-file>.umf (default ProbableBells)
           -O                     Print trace output
)*RAW*") ;
  exit (status) ;
  }
