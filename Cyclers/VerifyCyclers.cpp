// VerifyCyclers.cpp: verification program for Cyclers decider
//
// To invoke:
//
//   VerifyCyclers <param> <param>...
//     <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
//              -V<verification file>  Output file: verification data for decided machines
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

#include "../bbchallenge.h"

#define VERIF_INFO_LENGTH 24

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

class CyclerVerifier : public TuringMachine
  {
public:
  CyclerVerifier() : TuringMachine (MAX_SPACE)
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
  CommandLineParams::Parse (argc, argv) ;
  TuringMachineReader Reader (CommandLineParams::SeedDatabaseFile.c_str()) ;
  FILE* fpVerify = fopen (CommandLineParams::VerificationFile.c_str(), "rb") ;
  if (fpVerify == 0)
    printf ("File \"%s\" not found\n", CommandLineParams::VerificationFile.c_str()), exit (1) ;

  uint32_t nEntries = Read32 (fpVerify) ;
  int LastPercent = -1 ;
  uint8_t MachineSpec[MACHINE_SPEC_SIZE] ;
  CyclerVerifier Verifier ;

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
    if (DeciderTag (Read32 (fpVerify)) != DeciderTag::CYCLER)
      printf ("\nUnrecognised DeciderTag\n") ;

    Reader.Read (SeedDatabaseIndex, MachineSpec) ;
    Verifier.Verify (SeedDatabaseIndex, MachineSpec, fpVerify) ;
    }

  Timer = clock() - Timer ;

  // Check that we have reached the end of the verification file
  if (fread (MachineSpec, 1, 1, fpVerify) != 0)
    printf ("File too long!\n"), exit (1) ;
  fclose (fpVerify) ;

  printf ("\n%d Cyclers verified\n", nEntries) ;
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
  <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
           -V<verification file>  Decider Verification File for decided machines
)*RAW*") ;
  exit (status) ;
  }
