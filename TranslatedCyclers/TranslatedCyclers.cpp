#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <string>
#include <boost/thread.hpp>

#include "TranslatedCycler.h"

#define CHUNK_SIZE 1024 // Number of machines to assign to each thread

class CommandLineParams
  {
public:
  static std::string SeedDatabaseFile ;
  static std::string InputFile ;
  static std::string VerificationFile ;
  static std::string UndecidedFile ;
  static uint32_t TimeLimit ;     static bool TimeLimitPresent ;
  static uint32_t SpaceLimit ;    static bool SpaceLimitPresent ;
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
uint32_t CommandLineParams::TimeLimit ;     bool CommandLineParams::TimeLimitPresent ;
uint32_t CommandLineParams::SpaceLimit ;    bool CommandLineParams::SpaceLimitPresent ;
uint32_t CommandLineParams::nThreads ;      bool CommandLineParams::nThreadsPresent ;
uint32_t CommandLineParams::TestMachine ;   bool CommandLineParams::TestMachinePresent ;
uint32_t CommandLineParams::MachineLimit ;  bool CommandLineParams::MachineLimitPresent ;
bool CommandLineParams::TraceOutput ;

int main (int argc, char** argv)
  {
  CommandLineParams::Parse (argc, argv) ;

  TuringMachineReader Reader (CommandLineParams::SeedDatabaseFile.c_str()) ;

#if 0
  // Period 7,129,704, offset 512 (to the right) and has started cycling by step 536,870,912
  uint8_t SpecialSpec[MACHINE_SPEC_SIZE] =
    {
    1,0,2, 0,1,4, 1,0,3, 1,1,4, 1,1,4, 0,0,3, 0,1,1, 1,1,2, 0,0,0, 0,0,0
    } ;
    {
    TranslatedCycler Decider (CommandLineParams::TimeLimit,
      CommandLineParams::SpaceLimit, CommandLineParams::TraceOutput) ;
    Decider.Clone = new TranslatedCycler (CommandLineParams::TimeLimit,
      CommandLineParams::SpaceLimit, CommandLineParams::TraceOutput) ;
    Decider.SeedDatabaseIndex = 999 ;
    printf ("%d\n", Decider.Run (SpecialSpec, 0)) ;
    Decider.Initialise (999, SpecialSpec) ;
    for (int i = 0 ; i < 315850088 ; i++)
      if (Decider.Step() != StepResult::OK)
        printf ("Error 1\n"), exit (1) ;
    *Decider.Clone = Decider ;
    Decider.Clone -> Leftmost = Decider.Clone -> Rightmost = Decider.TapeHead ;
    for (int i = 0 ; i < 7129704 ; i++)
      if (Decider.Clone -> Step() != StepResult::OK)
        printf ("Error 1\n"), exit (1) ;
    if (Decider.Clone -> State != Decider.State)
      printf ("Error 2\n"), exit (1) ;
    if (Decider.Clone -> TapeHead != Decider.TapeHead + 512)
      printf ("Error 3\n"), exit (1) ;
    if (Decider.Clone -> Rightmost != Decider.Rightmost + 512)
      printf ("Error 4\n"), exit (1) ;
    if (memcmp (Decider.Tape + Decider.Clone -> Leftmost,
                Decider.Clone -> Tape + Decider.Clone -> Leftmost + 512,
                Decider.Rightmost - Decider.Clone -> Leftmost + 1))
      printf ("Error 5\n"), exit (1) ;
    printf ("OK\n") ;
    printf ("%d %d %d\n", Decider.Leftmost, Decider.Clone -> Leftmost, Decider.Clone -> Rightmost) ;
    exit (0) ;
    } ;
#endif

  uint8_t MachineSpec[MACHINE_SPEC_SIZE] ;
  if (CommandLineParams::TestMachinePresent)
    {
    TranslatedCycler Decider (CommandLineParams::TimeLimit,
      CommandLineParams::SpaceLimit, CommandLineParams::TraceOutput) ;
    Decider.Clone = new TranslatedCycler (CommandLineParams::TimeLimit,
      CommandLineParams::SpaceLimit, CommandLineParams::TraceOutput) ;
    Decider.SeedDatabaseIndex = CommandLineParams::TestMachine ;
    Reader.Read (Decider.SeedDatabaseIndex, MachineSpec) ;
    printf ("%d\n", Decider.Run (MachineSpec, 0)) ;
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

  FILE* fpVerif = 0 ;
  if (!CommandLineParams::VerificationFile.empty())
    {
    fpVerif = fopen (CommandLineParams::VerificationFile.c_str(), "wb") ;
    if (fpVerif == NULL)
      printf ("Can't open output file \"%s\"\n", CommandLineParams::VerificationFile.c_str()), exit (1) ;
    }

  // Count the space-limited machines in the input file (this is just
  // so we can give informative percentages in the progress report)
  uint32_t nTimeLimited = 0 ;
  uint32_t nSpaceLimited = 0 ;
  uint32_t nTotal = InputFileSize >> 2 ;
  for (uint32_t i = 0 ; i < nTotal ; i++)
    {
    if (Read32 (fpin) < Reader.nTimeLimited) nTimeLimited++ ;
    else nSpaceLimited++ ;
    }
  if (fseek (fpin, 0, SEEK_SET))
    printf ("fseek failed\n"), exit (1) ;

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

  TranslatedCycler** DeciderArray = new TranslatedCycler*[CommandLineParams::nThreads] ;
  uint32_t** MachineIndexList = new uint32_t*[CommandLineParams::nThreads] ;
  uint8_t** MachineSpecList = new uint8_t*[CommandLineParams::nThreads] ;
  uint8_t** VerificationEntryList = new uint8_t*[CommandLineParams::nThreads] ;
  uint32_t* ChunkSize = new uint32_t[CommandLineParams::nThreads] ;
  for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
    {
    DeciderArray[i] = new TranslatedCycler (CommandLineParams::TimeLimit,
      CommandLineParams::SpaceLimit, CommandLineParams::TraceOutput) ;
    DeciderArray[i] -> Clone = new TranslatedCycler (CommandLineParams::TimeLimit,
      CommandLineParams::SpaceLimit, CommandLineParams::TraceOutput) ;
    MachineIndexList[i] = new uint32_t[CHUNK_SIZE] ;
    MachineSpecList[i] = new uint8_t[MACHINE_SPEC_SIZE * CHUNK_SIZE] ;
    VerificationEntryList[i] = new uint8_t[VERIF_ENTRY_LENGTH * CHUNK_SIZE] ;
    }

  uint32_t nDecided = 0 ;
  uint32_t nCompleted = 0 ;
  int LastPercent = -1 ;

  if (CommandLineParams::MachineLimitPresent) nSpaceLimited = CommandLineParams::MachineLimit ;
  while (nCompleted < nSpaceLimited)
    {
    uint32_t nRemaining = nSpaceLimited - nCompleted ;
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
        uint32_t MachineIndex = Read32 (fpin) ;
        while (MachineIndex < Reader.nTimeLimited)
          {
          Write32 (fpUndecided, MachineIndex) ;
          nTimeLimited-- ;
          MachineIndex = Read32 (fpin) ;
          }
        MachineIndexList[i][j] = MachineIndex ;
        Reader.Read (MachineIndex, MachineSpecList[i] + j * MACHINE_SPEC_SIZE) ;
        }

      ThreadList[i] = new boost::thread (&TranslatedCycler::ThreadFunction,
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
          }
        else Write32 (fpUndecided, MachineIndexList[i][j]) ;
        MachineSpec += MACHINE_SPEC_SIZE ;
        VerificationEntry += VERIF_ENTRY_LENGTH ;
        nCompleted++ ;
        }
      }

    int Percent = (nCompleted * 100LL) / nSpaceLimited ;
    if (Percent != LastPercent)
      {
      LastPercent = Percent ;
      printf ("\r%d%% %d %d", Percent, nCompleted, nDecided) ;
      fflush (stdout) ;
      }
    }
  printf ("\n") ;

  // Just in case the input file was not sorted, check for stragglers
  while (nTimeLimited--)
    Write32 (fpUndecided, Read32 (fpin)) ;

  // Check that we've reached the end of the input file
  if (!CommandLineParams::MachineLimitPresent && fread (MachineSpec, 1, 1, fpin) != 0)
    printf ("\nInput file too long!\n"), exit (1) ;

  if (fpUndecided) fclose (fpUndecided) ;

  if (fpVerif)
    {
    // Write the verification file header
    if (fseek (fpVerif, 0 , SEEK_SET))
      printf ("\nfseek failed\n"), exit (1) ;
    Write32 (fpVerif, nDecided) ;
    fclose (fpVerif) ;
    }

  Timer = clock() - Timer ;

  fclose (fpin) ;

  printf ("\nDecided %d out of %d\n", nDecided, nSpaceLimited) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;

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

    MachineSpecList += MACHINE_SPEC_SIZE ;
    VerificationEntryList += VERIF_ENTRY_LENGTH ;
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
  if (!SpaceLimitPresent) SpaceLimit = 100000 ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf (R"*RAW*(
TranslatedCyclers <param> <param>...
  <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
           -I<input file>         Input file: list of machines to be analysed
           -V<verification file>  Output file: verification data for decided machines
           -U<undecided file>     Output file: remaining undecided machines
           -T<time limit>         Max no. of steps
           -S<space limit>        Max absolute value of tape head
           -X<test machine>       Machine to test
           -M<threads>            Number of threads to use
           -L<machine limit>      Max no. of machines to test
           -O                     Print trace output
)*RAW*") ;
  exit (status) ;
  }
