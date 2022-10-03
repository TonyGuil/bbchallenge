// VerifyTranslatedCyclers.cpp: verification program for Translated Cyclers decider
//
// To invoke:
//
//   VerifyTranslatedCyclers <param> <param>...
//     <param>: -D<database>           Seed database file (defaults to SeedDatabase.bin)
//              -V<verification file>  Output file: verification data for decided machines
//
// Format of verification info:
//
//   int32_t Leftmost
//   int32_t Rightmost
//   uint32_t FinalState
//   int32_t InitialTapeHead
//   int32_t FinalTapeHead
//   uint32_t InitialStepCount
//   uint32_t FinalStepCount
//   uint32_t MatchLength

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <string>

#include "../bbchallenge.h"

#define VERIF_INFO_LENGTH 32

class CommandLineParams
  {
public:
  static std::string SeedDatabaseFile ;
  static std::string VerificationFile ;
  static void Parse (int argc, char** argv) ;
  static void PrintHelpAndExit [[noreturn]] (int status) ;
  } ;

std::string CommandLineParams::SeedDatabaseFile ;
std::string CommandLineParams::VerificationFile ;

class TranslatedCyclerVerifier : public TuringMachine
  {
public:
  TranslatedCyclerVerifier() : TuringMachine (0, MAX_SPACE)
    {
    MatchContents = new uint8_t[2 * SpaceLimit + 1] ;
    MaxSteps = MaxMatchLength = MaxPeriod = MaxShift = 0 ;
    MinLeftmost = MaxRightmost = 0 ;
    }
  void Verify (uint32_t SeedDatabaseIndex,
    const uint8_t* MachineSpec, FILE* fp, bool TranslateLeft) ;
  uint8_t* MatchContents ;

  // Stats
  uint32_t MaxSteps ;
  int MinLeftmost ;
  int MaxRightmost ;
  uint32_t MaxMatchLength ;
  uint32_t MaxPeriod ;
  uint32_t MaxShift ;
  } ;

int main (int argc, char** argv)
  {
  CommandLineParams::Parse (argc, argv) ;
  TuringMachineReader Reader (CommandLineParams::SeedDatabaseFile.c_str()) ;
  FILE* fpVerify = fopen (CommandLineParams::VerificationFile.c_str(), "rb") ;
  if (fpVerify == 0)
    printf ("File \"%s\" not found\n", CommandLineParams::VerificationFile.c_str()), exit (1) ;

  uint32_t nEntries = Read32 (fpVerify) ;
  int LastPercent = -1 ;
  uint8_t MachineSpec[MACHINE_SPEC_SIZE] ;
  TranslatedCyclerVerifier Verifier ;

  clock_t Timer = clock() ;

  for (uint32_t Entry = 0 ; Entry < nEntries ; Entry++)
    {
    int Percent = ((Entry + 1) * 100LL) / nEntries ;
    if (Percent != LastPercent)
      {
      printf ("\r%d%%", Percent) ;
      LastPercent = Percent ;
      }

    uint32_t SeedDatabaseIndex = Read32 (fpVerify) ;
    bool TranslateLeft ;
    switch (DeciderTag (Read32 (fpVerify)))
      {
      case DeciderTag::TRANSLATED_CYCLERS_LEFT: TranslateLeft = true ; break ;
      case DeciderTag::TRANSLATED_CYCLERS_RIGHT: TranslateLeft = false ; break ;
      default: printf ("\nUnrecognised DeciderTag\n") ; exit (1) ;
      }

    Reader.Read (SeedDatabaseIndex, MachineSpec) ;
    Verifier.Verify (SeedDatabaseIndex, MachineSpec, fpVerify, TranslateLeft) ;
    }

  Timer = clock() - Timer ;

  if (fread (MachineSpec, 1, 1, fpVerify) != 0)
    printf ("File too long!\n"), exit (1) ;
  fclose (fpVerify) ;
  printf ("\n%d TranslatedCyclers verified\n", nEntries) ;
  printf ("Max %d steps\n", Verifier.MaxSteps) ;
  printf ("Max match length %d\n", Verifier.MaxMatchLength) ;
  printf ("Max period %d\n", Verifier.MaxPeriod) ;
  printf ("Max shift %d\n", Verifier.MaxShift) ;
  printf ("%d <= TapeHead <= %d\n", Verifier.MinLeftmost, Verifier.MaxRightmost) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;
  }

void TranslatedCyclerVerifier::Verify (uint32_t SeedDatabaseIndex,
  const uint8_t* MachineSpec, FILE* fp, bool TranslateLeft)
  {
  Initialise (SeedDatabaseIndex, MachineSpec) ;

  // Read the verification data from the input file
  if (Read32 (fp) != VERIF_INFO_LENGTH)
    printf ("Invalid TranslatedCyclers verification data\n"), exit (1) ;
  int32_t ExpectedLeftmost = Read32 (fp) ;
  int32_t ExpectedRightmost = Read32 (fp) ;
  uint32_t FinalState = Read32 (fp) ;
  int32_t InitialTapeHead = Read32 (fp) ;
  int32_t FinalTapeHead = Read32 (fp) ;
  uint32_t InitialStepCount = Read32 (fp) ;
  uint32_t FinalStepCount = Read32 (fp) ;
  uint32_t MatchLength = Read32 (fp) ;

  // Perform some sanity checks on the data
  if (ExpectedLeftmost > 0)
    printf ("Error: Leftmost = %d is positive\n", ExpectedLeftmost), exit (1) ;
  if (ExpectedRightmost < 0)
    printf ("Error: Rightmost = %d is negative\n", ExpectedRightmost), exit (1) ;
  if (FinalState == 0 || FinalState > 5)
    printf ("Invalid Final State %d\n", FinalState), exit (1) ; ;
  if (InitialTapeHead < ExpectedLeftmost || InitialTapeHead > ExpectedRightmost)
    printf ("Invalid InitialTapeHead out of bounds\n"), exit (1) ;
  if (FinalStepCount < InitialStepCount)
    printf ("FinalStepCount %d >= InitialStepCount %d\n", FinalStepCount, InitialStepCount), exit (1) ;
  if (TranslateLeft)
    {
    if (FinalTapeHead != ExpectedLeftmost)
      printf ("FinalTapeHead != ExpectedLeftmost\n"), exit (1) ;
    }
  else
    {
    if (FinalTapeHead != ExpectedRightmost)
      printf ("FinalTapeHead != ExpectedRightmost\n"), exit (1) ;
    }

  // Update the stats
  if (FinalStepCount > MaxSteps) MaxSteps = FinalStepCount ;
  if (ExpectedLeftmost < MinLeftmost) MinLeftmost = ExpectedLeftmost ;
  if (ExpectedRightmost > MaxRightmost) MaxRightmost = ExpectedRightmost ;
  if (MatchLength > MaxMatchLength) MaxMatchLength = MatchLength ;
  if (FinalStepCount - InitialStepCount > MaxPeriod) MaxPeriod = FinalStepCount - InitialStepCount ;
  if (FinalTapeHead - InitialTapeHead > (int)MaxShift) MaxShift = FinalTapeHead - InitialTapeHead ;

  // To perform the verification on a left- (resp. right-) Translated Cycler,
  // we must check that:
  //  (i) the final state is the same as the initial state.
  //  (ii) the initial congiguration is a left (resp. right) record -- that is,
  //       the tape head is in its leftmost (resp. rightmost) position so far.
  //       (Although our Decided program only considers absolute records, we
  //       allow equal records when checking; it eases the checks required, and
  //       it still results in a valid Translated Cycler.)
  //  (iii) the final configuration is also a record.
  //  (iv) between the initial and final configurations, the tape head never
  //       strays further than MatchLength bytes to the right (resp. left)
  //       of the initial tape head.
  //  (v) the leftmost (resp. rightmost) MatchLength bytes in the final
  //      configuration match the leftmost (resp. rightmost) MatchLength
  //      bytes in the initial configuration.

  // Now set Leftmost = InitialTapeHead - MatchLength + 1 (to ensure that the tape head doesn't stray into the forbidden zone) and Rightmost = FinalTapeHead (to ensure that the final state is a record state), and run the machine for a further (FinalStepCount - InitialStepCount) steps, checking that the Leftmost and Rightmost bounds are not exceeded.
  // Check that:
  //- state is equal to FinalState
  //- the tape head is equal to FinalTapeHead
  //- the final MatchLength bytes of the tape are equal to MatchArray

  // Point (ii): Restrict the tape head to ensure that the initial state is
  // really a record
  if (TranslateLeft) Tape[InitialTapeHead - 1] = Tape[ExpectedRightmost + 1] = TAPE_SENTINEL ;
  else Tape[ExpectedLeftmost - 1] = Tape[InitialTapeHead + 1] = TAPE_SENTINEL ;

  while (StepCount < FinalStepCount)
    {
    if (StepCount == InitialStepCount)
      {
      // Point (i): Check that State and TapeHead are as expected
      if (State != FinalState || TapeHead != InitialTapeHead)
        printf ("Initial state mismatch\n"), exit (1) ;

      // Point (iii) and (iv): move the right (resp. left) tape sentinel
      // to stop the tape head straying to the right (resp. left) of the
      // MatchLength bytes
      if (TranslateLeft)
        {
        memcpy (MatchContents, Tape + TapeHead, MatchLength) ;
        // We can now write beyond the initial tape head...
        Tape[InitialTapeHead - 1] = 0 ;
        // ...but not too far to the right
        Tape[TapeHead + MatchLength] = Tape[ExpectedLeftmost - 1] = TAPE_SENTINEL ;
        }
      else
        {
        memcpy (MatchContents, Tape + TapeHead - MatchLength + 1, MatchLength) ;
        // We can now write beyond the initial tape head...
        Tape[InitialTapeHead + 1] = 0 ;
        // ...but not too far to the left
        Tape[TapeHead - MatchLength] = Tape[ExpectedRightmost + 1] = TAPE_SENTINEL ;
        }
      }

    switch (Step())
      {
      case StepResult::OK:
        break ;

      case StepResult::OUT_OF_BOUNDS:
        printf ("Tape head out of bounds\n") ;
        exit (1) ;

      case StepResult::HALT:
        printf ("Unexpected HALT state reached\n") ;
        exit (1) ;
      }
    }

  // Check that the final state and tape head are as expected
  if (State != FinalState || TapeHead != FinalTapeHead)
    printf ("Final state mismatch\n"), exit (1) ;

  // Check that the leftmost (resp. rightmost) MatchLength bytes match those
  // of the initial state
  if (TranslateLeft)
    {
    if (memcmp (Tape + FinalTapeHead, MatchContents, MatchLength))
      printf ("Final tape mismatch\n"), exit (1) ;
    }
  else
    {
    if (memcmp (Tape + FinalTapeHead - MatchLength + 1, MatchContents, MatchLength))
      printf ("Final tape mismatch\n"), exit (1) ;
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

      default:
        printf ("Invalid parameter \"%s\"\n", argv[0]) ;
        PrintHelpAndExit (1) ;
      }
    }

  if (VerificationFile.empty()) printf ("Verification file not specified\n"), PrintHelpAndExit (1) ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf (R"*RAW*(
VerifyCyclers <param> <param>...
  <param>: -D<database>           Seed database file (defaults to SeedDatabase.bin)
           -V<verification file>  Decider Verification File for decided machines
)*RAW*") ;
  exit (status) ;
  }
