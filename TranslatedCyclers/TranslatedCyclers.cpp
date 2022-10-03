// TranslatedCyclers.cpp
//
// Decider for TranslatedCyclers
//
// To run:
//   TranslatedCyclers <param> <param>...
//     <param>: -D<database>           Seed database file (defaults to SeedDatabase.bin)
//              -I<input file>         Input file: list of machines to be analysed (umf)
//              -V<verification file>  Output file: verification data for decided machines (dvf)
//              -U<undecided file>     Output file: remaining undecided machines (umf)
//              -T<time limit>         Max no. of steps
//              -S<space limit>        Max absolute value of tape head
//              -W<workspace size>     Tape history workspace
//              -M<threads>            Number of threads to use
//
// Parameters -I, -V, -U, and -T are compulsory. Other parameters were mainly for tuning
// during development, and can be omitted.
//
// Format of Decider Verification File (32-bit big-endian integers, signed or unsigned):
//
// DeciderSpecificInfo format:
//   uint nEntries
//   VerificationEntry[nEntries]
// 
//   VerificationEntry format:
//     uint SeedDatabaseIndex
//     uint DeciderType     -- 2 = TranslatedCyclers (translated to the right)
//                          -- 3 = TranslatedCyclers (translated to the left)
//     uint InfoLength = 32 -- Length of decider-specific info
//     -- DeciderSpecificInfo
//     -- An initial configuration matches a final configuration translated left or right.
//     -- Leftmost and Rightmost are for the convenience of the Verifier, and not strictly necessary.
//     int Leftmost            -- Leftmost tape head position
//     int Rightmost           -- Rightmost tape head position
//     uint State              -- State of machine in initial and final configurations
//     int InitialTapeHead     -- Tape head in initial configuration
//     int FinalTapeHead       -- Tape head in final configuration
//     uint InitialStepCount   -- Number of steps to reach initial configuration
//     uint FinalStepCount     -- Number of steps to reach final configuration
//     uint MatchLength        -- Length of match
//
// NOTE: This code will NOT decide (non-translated) Cyclers, because it only
// looks for a match when a Leftmost/Rightmost record is broken, not when it
// is simply matched. This saves us a lot of time.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <string>
#include <boost/thread.hpp>

#include "../bbchallenge.h"

#define CHUNK_SIZE 1024 // Number of machines to assign to each thread

#define VERIF_INFO_LENGTH 32 // Length of DeciderSpecificInfo in Verification File

class CommandLineParams
  {
public:
  static std::string SeedDatabaseFile ;
  static std::string InputFile ;
  static std::string VerificationFile ;
  static std::string UndecidedFile ;
  static uint32_t TimeLimit ;     static bool TimeLimitPresent ;
  static uint32_t SpaceLimit ;    static bool SpaceLimitPresent ;
  static uint32_t WorkspaceSize ; static bool WorkspaceSizePresent ;
  static uint32_t nThreads ;      static bool nThreadsPresent ;
  static void Parse (int argc, char** argv) ;
  static void PrintHelpAndExit [[noreturn]] (int status) ;
  } ;

std::string CommandLineParams::SeedDatabaseFile ;
std::string CommandLineParams::InputFile ;
std::string CommandLineParams::VerificationFile ;
std::string CommandLineParams::UndecidedFile ;
uint32_t CommandLineParams::TimeLimit ;     bool CommandLineParams::TimeLimitPresent ;
uint32_t CommandLineParams::SpaceLimit ;    bool CommandLineParams::SpaceLimitPresent ;
uint32_t CommandLineParams::WorkspaceSize ; bool CommandLineParams::WorkspaceSizePresent ;
uint32_t CommandLineParams::nThreads ;      bool CommandLineParams::nThreadsPresent ;

class TranslatedCycler : public TuringMachine
  {
public:
  TranslatedCycler (uint32_t TimeLimit, uint32_t SpaceLimit)
  : TuringMachine (TimeLimit, SpaceLimit)
    {
    // Allocate workspace
    RightRecordList = new RecordData[SpaceLimit] ;
    LeftRecordList = new RecordData[SpaceLimit] ;
    if (CommandLineParams::WorkspaceSizePresent)
      WorkspaceSize = CommandLineParams::WorkspaceSize ;
    else WorkspaceSize = SpaceLimit * SpaceLimit ;
    Workspace = new uint8_t[WorkspaceSize] ;
    WorkspaceUsed = 0 ;
    if (Zeroes == 0)
      {
      Zeroes = new uint8_t[2 * SpaceLimit + 1] ;
      memset (Zeroes, 0, 2 * SpaceLimit + 1) ;
      }
    }

  void ThreadFunction (int nMachines, const uint32_t* MachineIndexList,
    const uint8_t* MachineSpecList, uint8_t* VerificationEntryList) ;

  uint32_t WorkspaceSize ;
  uint32_t WorkspaceUsed ;

private:

  void Run (uint32_t SeedDatabaseIndex, const uint8_t* MachineSpec, uint8_t* VerificationEntry) ;

  struct RecordData
    {
    uint32_t StepCount ;
    uint8_t* TapeContents ;
    int16_t WaterMark ;
    int16_t Leftmost ;
    int16_t Rightmost ;
    uint8_t State ;
    } ;
  RecordData* RightRecordList ;
  RecordData* LeftRecordList ;

  // When we have a time limit in the millions, we have to husband our resources.
  // The most precious resource for us is memory: remembering the tape contents
  // of all previous record-breaking configurations requires many megabytes. So
  // we allocate a huge Workspace for each thread, and for each configuration, we
  // just save the tape contents between the left- and rightmost tape head
  // positions. WorkspacePtr in the Run function keeps track of the used Workspace.
  uint8_t* Workspace ;

  // We want a reasonably fast way to check whether a stretch of tape contains only
  // zeroes. So we allocate a static array of all zeroes, and use memcmp:
  static uint8_t* Zeroes ; // for comparing to zero
  } ;

uint8_t* TranslatedCycler::Zeroes = 0 ;

int main (int argc, char** argv)
  {
  CommandLineParams::Parse (argc, argv) ;

  TuringMachineReader Reader (CommandLineParams::SeedDatabaseFile.c_str()) ;

  FILE* fpin = fopen (CommandLineParams::InputFile.c_str(), "rb") ;
  if (fpin == NULL) printf ("Can't open input file\n"), exit (1) ;
  if (fseek (fpin, 0, SEEK_END)) printf ("fseek failed\n"), exit (1) ;
  long InputFileSize = ftell (fpin) ;
  if (fseek (fpin, 0, SEEK_SET)) printf ("fseek failed\n"), exit (1) ;

  FILE* fpUndecided = fopen (CommandLineParams::UndecidedFile.c_str(), "wb") ;
  if (fpUndecided == NULL)
    printf ("Can't open output file \"%s\"\n", CommandLineParams::UndecidedFile.c_str()), exit (1) ;

  FILE* fpVerif = fopen (CommandLineParams::VerificationFile.c_str(), "wb") ;
  if (fpVerif == NULL)
    printf ("Can't open output file \"%s\"\n", CommandLineParams::VerificationFile.c_str()), exit (1) ;

  if (!CommandLineParams::SpaceLimitPresent)
    CommandLineParams::SpaceLimit = 5 * std::sqrt (CommandLineParams::TimeLimit) ;

  int nTimeLimited = Read32 (fpin) ;
  int nSpaceLimited = Read32 (fpin) ;
  int nTotal = nTimeLimited + nSpaceLimited ;

  if (InputFileSize != 4 * (nTotal + 2))
    printf ("File size discrepancy\n"), exit (1) ;

  // Write dummy headers
  Write32 (fpUndecided, 0) ;
  Write32 (fpUndecided, 0) ;
  Write32 (fpVerif, 0) ;

  // The time-limited machines are not expected to yield any Translated Cyclers,
  // so we just copy them to the umf:
  printf ("Copying time-limited machines...") ;
  for (int Index = 0 ; Index < nTimeLimited ; Index++)
    Write32 (fpUndecided, Read32 (fpin)) ;
  printf ("Done\n") ;

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

  TranslatedCycler** TCArray = new TranslatedCycler*[CommandLineParams::nThreads] ;
  uint32_t** MachineIndexList = new uint32_t*[CommandLineParams::nThreads] ;
  uint8_t** MachineSpecList = new uint8_t*[CommandLineParams::nThreads] ;
  uint8_t** VerificationEntryList = new uint8_t*[CommandLineParams::nThreads] ;
  uint32_t* ChunkSize = new uint32_t[CommandLineParams::nThreads] ;
  for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
    {
    TCArray[i] = new TranslatedCycler (CommandLineParams::TimeLimit, CommandLineParams::SpaceLimit) ;
    MachineIndexList[i] = new uint32_t[CHUNK_SIZE] ;
    MachineSpecList[i] = new uint8_t[MACHINE_SPEC_SIZE * CHUNK_SIZE] ;
    VerificationEntryList[i] = new uint8_t[VERIF_ENTRY_LENGTH * CHUNK_SIZE] ;
    }

  int nDecided = 0 ;
  int LastPercent = -1 ;
  int nCompleted = 0 ;

  clock_t Timer = clock() ;

  // Analyse the space-limited machines
  while (nCompleted < nSpaceLimited)
    {
    uint32_t nRemaining = nSpaceLimited - nCompleted ;
    if (nRemaining >= CommandLineParams::nThreads * CHUNK_SIZE)
      {
      for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++) ChunkSize[i] = CHUNK_SIZE ;
      nCompleted += CommandLineParams::nThreads * CHUNK_SIZE ;
      }
    else
      {
      for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
        {
        ChunkSize[i] = nRemaining / (CommandLineParams::nThreads - i) ;
        nRemaining -= ChunkSize[i] ;
        }
      nCompleted = nSpaceLimited ;
      }

    for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
      {
      for (uint32_t j = 0 ; j < ChunkSize[i] ; j++)
        {
        MachineIndexList[i][j] = Read32 (fpin) ;
        Reader.Read (MachineIndexList[i][j], MachineSpecList[i] + j * MACHINE_SPEC_SIZE) ;
        }

      ThreadList[i] = new boost::thread (TranslatedCycler::ThreadFunction,
        TCArray[i], ChunkSize[i], MachineIndexList[i], MachineSpecList[i], VerificationEntryList[i]) ;
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
        else Write32 (fpUndecided, MachineIndexList[i][j]) ;
        MachineSpec += MACHINE_SPEC_SIZE ;
        VerificationEntry += VERIF_ENTRY_LENGTH ;
        }
      }

    int Percent = (nCompleted * 100LL) / nSpaceLimited ;
    if (Percent != LastPercent)
      {
      LastPercent = Percent ;
      printf ("\r%d%% %d %d", Percent, nCompleted, nDecided) ;
      }
    }

  Timer = clock() - Timer ;

  printf ("\n") ;

  // Check that we reached the end of the input file
  if (fread (MachineSpecList[0], 1, 1, fpin) != 0) printf ("Input file too long!\n"), exit (1) ;

  if (fseek (fpVerif, 0 , SEEK_SET))
    printf ("fseek failed\n"), exit (1) ;
  Write32 (fpVerif, nDecided) ;
  fclose (fpVerif) ;

  if (fseek (fpUndecided, 0 , SEEK_SET))
    printf ("\nfseek failed\n"), exit (1) ;
  Write32 (fpUndecided, nTimeLimited) ;
  Write32 (fpUndecided, nSpaceLimited - nDecided) ;

  fclose (fpUndecided) ;
  fclose (fpin) ;

  uint32_t WorkspaceUsed = 0 ;
  for (uint32_t i = 0 ; i < CommandLineParams::nThreads ; i++)
    if (TCArray[i] -> WorkspaceUsed > WorkspaceUsed)
      WorkspaceUsed = TCArray[i] -> WorkspaceUsed ;

  printf ("\nDecided %d out of %d\n", nDecided, nTimeLimited + nSpaceLimited) ;
  printf ("Workspace: used %d bytes out of %d\n", WorkspaceUsed, TCArray[0] -> WorkspaceSize) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;
  }

void TranslatedCycler::ThreadFunction (int nMachines, const uint32_t* MachineIndexList,
  const uint8_t* MachineSpecList, uint8_t* VerificationEntryList)
  {
  while (nMachines--)
    {
    Run (*MachineIndexList++, MachineSpecList, VerificationEntryList) ;
    MachineSpecList += MACHINE_SPEC_SIZE ;
    VerificationEntryList += VERIF_ENTRY_LENGTH ;
    }
  }

void TranslatedCycler::Run (uint32_t SeedDatabaseIndex, const uint8_t* MachineSpec, uint8_t* VerificationEntry)
  {
  *(uint32_t*)(VerificationEntry + 4) = uint32_t (DeciderTag::NONE) ; // i.e. undecided

  Initialise (SeedDatabaseIndex, MachineSpec) ;

  uint32_t nRightRecords = 0 ;
  uint32_t nLeftRecords = 0 ;
  int LowWaterMark = TapeHead + 1 ;  // for right-translated matches
  int HighWaterMark = TapeHead - 1 ; // for left-translated matches
  int RightRecord = -1 ;
  int LeftRecord = 1 ;
  uint32_t WorkspacePtr = 0 ;

  while (StepCount < TimeLimit)
    {
    if (TapeHead > RightRecord)
      {
      if (Tape[TapeHead] == TAPE_SENTINEL) return ;

      // Check for matches
      for (uint32_t i = 0 ; i < nRightRecords ; i++) if (RightRecordList[i].State == State)
        {
        const RecordData& RD = RightRecordList[i] ;
        int nCells = RD.Rightmost - RD.WaterMark + 1 ;
        int nSaved = RD.Rightmost - RD.Leftmost + 1 ;
        bool Match ;

        // The number of tape cells we have to compare can exceed the number of
        // cells saved in the initial configuration. In this case we have to
        // compare with the saved bytes, and then compare against zero:
        if (nCells <= nSaved)
          Match = !memcmp (RD.TapeContents + nSaved - nCells,
            Tape + TapeHead - nCells + 1, nCells) ;
        else Match = !memcmp (Zeroes, Tape + TapeHead - nCells + 1, nCells - nSaved) &&
          !memcmp (RD.TapeContents, Tape + TapeHead - nSaved + 1, nSaved) ;

        if (Match)
          {
          Save32 (VerificationEntry, SeedDatabaseIndex) ;
          Save32 (VerificationEntry + 4, uint32_t (DeciderTag::TRANSLATED_CYCLERS_RIGHT)) ;
          Save32 (VerificationEntry + 8, VERIF_INFO_LENGTH) ;

          // DeciderSpecificInfo
          Save32 (VerificationEntry + 12, Leftmost) ;
          Save32 (VerificationEntry + 16, Rightmost) ;
          Save32 (VerificationEntry + 20, State) ;
          Save32 (VerificationEntry + 24, RD.Rightmost) ; // = RD.TapeHead
          Save32 (VerificationEntry + 28, TapeHead) ;
          Save32 (VerificationEntry + 32, RD.StepCount) ;
          Save32 (VerificationEntry + 36, StepCount) ;
          Save32 (VerificationEntry + 40, nCells) ;

          return ;
          }
        }

      // New rightmost record
      if (nRightRecords == SpaceLimit) return ;

      LowWaterMark = TapeHead ;

      if (WorkspacePtr + Rightmost - Leftmost + 1 > WorkspaceSize)
        printf ("WorkspaceSize exceeded\n"), exit (1) ;

      RecordData& RD = RightRecordList[nRightRecords++] ;
      RD.State = State ;
      RD.StepCount = StepCount ;
      RD.Leftmost = Leftmost ;
      RD.Rightmost = TapeHead ;
      RD.WaterMark = TapeHead ;
      RD.TapeContents = Workspace + WorkspacePtr ;
      memcpy (RD.TapeContents, Tape + Leftmost, Rightmost - Leftmost + 1) ;
      WorkspacePtr += Rightmost - Leftmost + 1 ;
      if (WorkspacePtr > WorkspaceUsed) WorkspaceUsed = WorkspacePtr ;

      RightRecord = TapeHead ;
      }

    if (TapeHead < LeftRecord)
      {
      if (Tape[TapeHead] == TAPE_SENTINEL) return ;

      // Check for matches
      for (uint32_t i = 0 ; i < nLeftRecords ; i++) if (LeftRecordList[i].State == State)
        {
        const RecordData& RD = LeftRecordList[i] ;
        int nCells = RD.WaterMark - RD.Leftmost + 1 ;
        int nSaved = RD.Rightmost - RD.Leftmost + 1 ;
        bool Match ;

        // The number of tape cells we have to compare can exceed the number of
        // cells saved in the initial configuration. In this case we have to
        // compare with the saved bytes, and then compare against zero:
        if (nCells <= nSaved)
          Match = !memcmp (RD.TapeContents, Tape + TapeHead, nCells) ;
        else Match = !memcmp (RD.TapeContents, Tape + TapeHead, nSaved) &&
          !memcmp (Zeroes, Tape + TapeHead + nSaved, nCells - nSaved) ;

        if (Match)
          {
          Save32 (VerificationEntry, SeedDatabaseIndex) ;
          Save32 (VerificationEntry + 4, uint32_t (DeciderTag::TRANSLATED_CYCLERS_LEFT)) ;
          Save32 (VerificationEntry + 8, VERIF_INFO_LENGTH) ;

          // DeciderSpecificInfo
          Save32 (VerificationEntry + 12,Leftmost) ;
          Save32 (VerificationEntry + 16,Rightmost) ;
          Save32 (VerificationEntry + 20,State) ;
          Save32 (VerificationEntry + 24,RD.Leftmost) ; // = RD.TapeHead
          Save32 (VerificationEntry + 28,TapeHead) ;
          Save32 (VerificationEntry + 32,RD.StepCount) ;
          Save32 (VerificationEntry + 36,StepCount) ;
          Save32 (VerificationEntry + 40,nCells) ;

          return ;
          }
        }

      // New leftmost record
      if (nLeftRecords == SpaceLimit) return ;

      HighWaterMark = TapeHead ;

      if (WorkspacePtr + Rightmost - Leftmost + 1 > WorkspaceSize)
        printf ("WorkspaceSize exceeded\n"), exit (1) ;

      RecordData& RD = LeftRecordList[nLeftRecords++] ;
      RD.State = State ;
      RD.StepCount = StepCount ;
      RD.Leftmost = TapeHead ;
      RD.Rightmost = Rightmost ;
      RD.WaterMark = TapeHead ;
      RD.TapeContents = Workspace + WorkspacePtr ;
      memcpy (RD.TapeContents, Tape + Leftmost, Rightmost - Leftmost + 1) ;
      WorkspacePtr += Rightmost - Leftmost + 1 ;
      if (WorkspacePtr > WorkspaceUsed) WorkspaceUsed = WorkspacePtr ;

      LeftRecord = TapeHead ;
      }

    if (TapeHead < LowWaterMark)
      {
      if (Tape[TapeHead] == TAPE_SENTINEL) return ;
      LowWaterMark = TapeHead ;
      for (int i = nRightRecords - 1 ; i >= 0 ; i--)
        {
        if (RightRecordList[i].WaterMark <= LowWaterMark) break ;
        RightRecordList[i].WaterMark = LowWaterMark ;
        }
      }

    if (TapeHead > HighWaterMark)
      {
      if (Tape[TapeHead] == TAPE_SENTINEL) return ;
      HighWaterMark = TapeHead ;
      for (int i = nLeftRecords - 1 ; i >= 0 ; i--)
        {
        if (LeftRecordList[i].WaterMark >= HighWaterMark) break ;
        LeftRecordList[i].WaterMark = HighWaterMark ;
        }
      }

    switch (Step())
      {
      case StepResult::OK: break ;
      case StepResult::OUT_OF_BOUNDS: return ;
      case StepResult::HALT:
        printf ("Unexpected HALT state reached! %d\n", StepCount) ;
        printf ("SeedIndex = %d, TapeHead = %d\n", Load32 (MachineSpec - 4), TapeHead) ;
        exit (1) ;
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

      case 'W':
        WorkspaceSize = atoi (&argv[0][2]) ;
        WorkspaceSizePresent = true ;
        break ;

      case 'M':
        nThreads = atoi (&argv[0][2]) ;
        nThreadsPresent = true ;
        break ;

      default:
        printf ("Invalid parameter \"%s\"\n", argv[0]) ;
        PrintHelpAndExit (1) ;
      }
    }

  if (InputFile.empty()) printf ("Input file not specified\n"), PrintHelpAndExit (1) ;
  if (VerificationFile.empty()) printf ("Verification file not specified\n"), PrintHelpAndExit (1) ;
  if (UndecidedFile.empty()) printf ("Undecided file not specified\n"), PrintHelpAndExit (1) ;

  if (!TimeLimitPresent) printf ("Time limit not specified\n"), PrintHelpAndExit (1) ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf (R"*RAW*(
TranslatedCyclers <param> <param>...
  <param>: -D<database>           Seed database file (defaults to SeedDatabase.bin)
           -I<input file>         Input file: list of machines to be analysed
           -V<verification file>  Output file: verification data for decided machines
           -U<undecided file>     Output file: remaining undecided machines
           -T<time limit>         Max no. of steps
           -S<space limit>        Max absolute value of tape head
           -W<workspace size>     Tape history workspace
           -M<threads>            Number of threads to use
)*RAW*") ;
  exit (status) ;
  }
