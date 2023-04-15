// VerifyCyclers <param> <param>...
//   <param>: -N<states>            Machine states (5 or 6)
//            -D<database>          Seed database file (defaults to ../SeedDatabase.bin)
//            -V<verification file> Input file: verification data to be checked
//
// Format of verification info:
//
//   int Leftmost
//   int Rightmost
//   uint State
//   int TapeHead
//   uint InitialStepCount
//   int FinalStepCount

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <string>

#include "../TuringMachine.h"
#include "../Params.h"

#define VERIF_INFO_LENGTH 24

class CommandLineParams : public VerifierParams
  {
public:
  void Parse (int argc, char** argv) ;
  virtual void PrintHelpAndExit (int status) override ;
  } ;

static CommandLineParams Params ;
static TuringMachineReader Reader ;

class CyclerVerifier : public TuringMachine
  {
public:
  CyclerVerifier (uint32_t  MachineStates)
  : TuringMachine (MachineStates, MAX_SPACE)
    {
    InitialTape = new uint8_t[2 * MAX_SPACE + 1] ;
    MaxSteps = 0 ;
    }
  void Verify (uint32_t SeedDatabaseIndex, const uint8_t* MachineSpec, FILE* fp) ;
  uint8_t* InitialTape ;

  // Stats
  uint32_t MaxSteps ;
  } ;

int main (int argc, char** argv)
  {
  Params.Parse (argc, argv) ;
  Params.CheckParameters() ;
  Params.OpenFiles() ;

  Reader.SetParams (&Params) ;

  int LastPercent = -1 ;
  uint8_t MachineSpec[MAX_MACHINE_SPEC_SIZE] ;
  CyclerVerifier Verifier (Params.MachineStates) ;

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

    uint32_t SeedDatabaseIndex = Read32 (Params.fpVerify) ;
    if (DeciderTag (Read32 (Params.fpVerify)) != DeciderTag::CYCLER)
      printf ("\nUnrecognised DeciderTag\n") ;

    Reader.Read (SeedDatabaseIndex, MachineSpec) ;
    Verifier.Verify (SeedDatabaseIndex, MachineSpec, Params.fpVerify) ;
    }

  Timer = clock() - Timer ;

  // Check that we have reached the end of the verification file
  if (!CheckEndOfFile (Params.fpVerify)) printf ("File too long!\n"), exit (1) ;
  fclose (Params.fpVerify) ;

  printf ("\n%d Cyclers verified\n", Reader.nMachines) ;
  printf ("Max %d steps\n", Verifier.MaxSteps) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;
  }

void CyclerVerifier::Verify (uint32_t SeedDatabaseIndex, const uint8_t* MachineSpec, FILE* fpVerify)
  {
  Initialise (SeedDatabaseIndex, MachineSpec) ;

  // Read the verification data from the input file
  if (Read32 (fpVerify) != VERIF_INFO_LENGTH)
    printf ("Invalid Cyclers verification data length\n"), exit (1) ;
  int32_t ExpectedLeftmost = Read32 (fpVerify) ;
  int32_t ExpectedRightmost = Read32 (fpVerify) ;
  uint32_t ExpectedState = Read32 (fpVerify) ;
  int32_t ExpectedTapeHead = Read32 (fpVerify) ;
  uint32_t InitialStepCount = Read32 (fpVerify) ;
  uint32_t FinalStepCount = Read32 (fpVerify) ;

  // Perform some sanity checks on the data
  if (ExpectedLeftmost > 0)
    printf ("Error: Leftmost = %d is positive\n", ExpectedLeftmost), exit (1) ;
  if (ExpectedRightmost < 0)
    printf ("Error: Rightmost = %d is negative\n", ExpectedRightmost), exit (1) ;
  if (ExpectedState == 0 || ExpectedState > 5)
    printf ("Invalid Expected State %d\n", ExpectedState), exit (1) ; ;
  if (ExpectedTapeHead < ExpectedLeftmost || ExpectedTapeHead > ExpectedRightmost)
    printf ("ExpectedTapeHead = %d is out of bounds\n", ExpectedTapeHead), exit (1) ; ;
  if (FinalStepCount < InitialStepCount)
    printf ("FinalStepCount %d >= InitialStepCount %d\n", FinalStepCount, InitialStepCount), exit (1) ;

  // Update the stats
  if (FinalStepCount > MaxSteps) MaxSteps = FinalStepCount ;

  // Run the machine for FinalStepCount steps
  Tape[ExpectedLeftmost - 1] = Tape[ExpectedRightmost + 1] = TAPE_SENTINEL ;
  while (StepCount < FinalStepCount)
    {
    if (StepCount == InitialStepCount)
      {
      // Check that the initial configuration's State and TapeHead are as expected
      if (State != ExpectedState || TapeHead != ExpectedTapeHead)
        printf ("Initial state mismatch\n"), exit (1) ;

      // Save the tape contents for checking against the final configuration
      memcpy (InitialTape, Tape - SpaceLimit, 2 * SpaceLimit + 1) ;
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

  // Check that the State and TapeHead are as expected
  if (State != ExpectedState || TapeHead != ExpectedTapeHead)
    printf ("Configuration mismatch\n"), exit (1) ;

  // Check that the tape contents are as expected
  if (memcmp (Tape - SpaceLimit, InitialTape, 2 * SpaceLimit + 1))
    printf ("Tape mismatch\n"), exit (1) ;

  // Check Leftmost and Rightmost (not really necessary)
  if (Leftmost != ExpectedLeftmost)
    printf ("Leftmost discrepancy\n"), exit (1) ;
  if (Rightmost != ExpectedRightmost)
    printf ("Rightmost discrepancy\n"), exit (1) ;
  }

void CommandLineParams::Parse (int argc, char** argv)
  {
  if (argc == 1) PrintHelpAndExit (0) ;

  for (argc--, argv++ ; argc ; argc--, argv++)
    if (!CommonParams::ParseParam (argv[0]))
      {
      printf ("Invalid parameter \"%s\"\n", argv[0]) ;
      PrintHelpAndExit (1) ;
      }

  if (VerificationFilename.empty()) printf ("Verification file not specified\n"), PrintHelpAndExit (1) ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf ("VerifyCyclers <param> <param>...\n") ;
  PrintHelp() ;
  exit (status) ;
  }
