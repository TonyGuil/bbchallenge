// VerifyTranslatedCyclers <param> <param>...
//   <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
//            -V<verification file>  Input file: verification data to be checked
//            -S<space limit>        Max absolute value of tape head
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

#include "../TuringMachine.h"
#include "../Params.h"

#define VERIF_INFO_LENGTH 32

class CommandLineParams : public VerifierParams
  {
public:
  uint32_t SpaceLimit = 50000 ; // Needed to verify #60054343
  void Parse (int argc, char** argv) ;
  virtual void PrintHelpAndExit (int status) ;
  } ;
static CommandLineParams Params ;

class TranslatedCyclerVerifier : public TuringMachine
  {
public:
  TranslatedCyclerVerifier (uint32_t MachineStates, uint32_t SpaceLimit)
  : TuringMachine (MachineStates, SpaceLimit)
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
  Params.Parse (argc, argv) ;
  Params.CheckParameters() ;
  Params.OpenFiles() ;

  TuringMachineReader Reader (&Params) ;

  int LastPercent = -1 ;
  uint8_t MachineSpec[MAX_MACHINE_SPEC_SIZE] ;
  TranslatedCyclerVerifier Verifier (Params.MachineStates, Params.SpaceLimit) ;

  clock_t Timer = clock() ;

  for (uint32_t Entry = 0 ; Entry < Reader.nMachines ; Entry++)
    {
    int Percent = ((Entry + 1) * 100LL) / Reader.nMachines ;
    if (Percent != LastPercent)
      {
      printf ("\r%d%%", Percent) ;
      fflush (stdout) ;
      LastPercent = Percent ;
      }

    uint32_t MachineIndex = Read32 (Params.fpVerify) ;
    bool TranslateLeft ;
    switch (DeciderTag (Read32 (Params.fpVerify)))
      {
      case DeciderTag::TRANSLATED_CYCLER_LEFT: TranslateLeft = true ; break ;
      case DeciderTag::TRANSLATED_CYCLER_RIGHT: TranslateLeft = false ; break ;
      default: printf ("\nUnrecognised DeciderTag\n") ; exit (1) ;
      }

    Reader.Read (MachineIndex, MachineSpec) ;
    Verifier.Verify (MachineIndex, MachineSpec, Params.fpVerify, TranslateLeft) ;
    }

  Timer = clock() - Timer ;

  if (fread (MachineSpec, 1, 1, Params.fpVerify) != 0)
    printf ("File too long!\n"), exit (1) ;
  fclose (Params.fpVerify) ;
  printf ("\n%d TranslatedCyclers verified\n", Reader.nMachines) ;
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
    printf ("%d: Invalid TranslatedCyclers verification data\n", SeedDatabaseIndex), exit (1) ;
  int32_t ExpectedLeftmost = Read32 (fp) ;
  int32_t ExpectedRightmost = Read32 (fp) ;
  uint32_t FinalState = Read32 (fp) ;
  int32_t InitialTapeHead = Read32 (fp) ;
  int32_t FinalTapeHead = Read32 (fp) ;
  uint32_t InitialStepCount = Read32 (fp) ;
  uint32_t FinalStepCount = Read32 (fp) ;
  int32_t MatchLength = Read32 (fp) ;

  // Perform some sanity checks on the data
  if (ExpectedLeftmost > 0)
    printf ("%d: Error: Leftmost = %d is positive\n", SeedDatabaseIndex, ExpectedLeftmost), exit (1) ;
  if (ExpectedRightmost < 0)
    printf ("%d: Error: Rightmost = %d is negative\n", SeedDatabaseIndex, ExpectedRightmost), exit (1) ;
  if (FinalState == 0 || FinalState > MachineStates)
    printf ("%d: Invalid Final State %d\n", SeedDatabaseIndex, FinalState), exit (1) ;
  if (InitialTapeHead < ExpectedLeftmost || InitialTapeHead > ExpectedRightmost)
    printf ("%d: Invalid InitialTapeHead out of bounds\n", SeedDatabaseIndex), exit (1) ;
  if (FinalStepCount < InitialStepCount)
    printf ("%d: FinalStepCount %d >= InitialStepCount %d\n",
      SeedDatabaseIndex, FinalStepCount, InitialStepCount), exit (1) ;
  if (TranslateLeft)
    {
    if (FinalTapeHead != ExpectedLeftmost)
      printf ("%d: FinalTapeHead != ExpectedLeftmost\n", SeedDatabaseIndex), exit (1) ;
    }
  else
    {
    if (FinalTapeHead != ExpectedRightmost)
      printf ("%d: FinalTapeHead != ExpectedRightmost\n", SeedDatabaseIndex), exit (1) ;
    }

  // Update the stats
  if (FinalStepCount > MaxSteps) MaxSteps = FinalStepCount ;
  if (ExpectedLeftmost < MinLeftmost) MinLeftmost = ExpectedLeftmost ;
  if (ExpectedRightmost > MaxRightmost) MaxRightmost = ExpectedRightmost ;
  if (MatchLength > (int32_t)MaxMatchLength) MaxMatchLength = MatchLength ;
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
        printf ("%d: Initial state mismatch\n", SeedDatabaseIndex), exit (1) ;

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
        printf ("%d: Tape head %d is out of bounds\n", SeedDatabaseIndex, TapeHead) ;
        exit (1) ;

      case StepResult::HALT:
        printf ("%d: Unexpected HALT state reached\n", SeedDatabaseIndex) ;
        exit (1) ;
      }
    }

  // Check that the final state and tape head are as expected
  if (State != FinalState || TapeHead != FinalTapeHead)
    printf ("%d: Final state mismatch\n", SeedDatabaseIndex), exit (1) ;

  // Check that the leftmost (resp. rightmost) MatchLength bytes match those
  // of the initial state
  if (TranslateLeft)
    {
    if (memcmp (Tape + FinalTapeHead, MatchContents, MatchLength))
      printf ("%d: Final tape mismatch\n", SeedDatabaseIndex), exit (1) ;
    }
  else
    {
    if (memcmp (Tape + FinalTapeHead - MatchLength + 1, MatchContents, MatchLength))
      printf ("%d: Final tape mismatch\n", SeedDatabaseIndex), exit (1) ;
    }
  }

void CommandLineParams::Parse (int argc, char** argv)
  {
  if (argc == 1) PrintHelpAndExit (0) ;

  for (argc--, argv++ ; argc ; argc--, argv++)
    {
    if (CommonParams::ParseParam (argv[0])) continue ;
    if (argv[0][0] != '-') printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
    switch (toupper (argv[0][1]))
      {
      case 'S':
        SpaceLimit = atoi (&argv[0][2]) ;
        break ;

      default:
        printf ("Invalid parameter \"%s\"\n", argv[0]) ;
        PrintHelpAndExit (1) ;
      }
    }

  if (VerificationFilename.empty()) printf ("Verification file not specified\n"), PrintHelpAndExit (1) ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf ("VerifyCyclers <param> <param>...") ;
  PrintHelp() ;
  printf (R"*RAW*(
           -S<space limit>       Max absolute value of tape head (default 50000)\n") ;
)*RAW*") ;
  exit (status) ;
  }
