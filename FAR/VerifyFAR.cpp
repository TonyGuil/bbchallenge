#include <time.h>
#include <string>
#include <vector>

#include "../bbchallenge.h"
#include "FAR.h"

//
// Command-line parameters
//

class CommandLineParams
  {
public:
  static std::string SeedDatabaseFile ;
  static std::string VerificationFile ;
  static uint32_t TestMachine ; static bool TestMachinePresent ;
  static bool CheckNFA ;
  static bool TraceOutput ;
  static void Parse (int argc, char** argv) ;
  static void PrintHelpAndExit [[noreturn]] (int status) ;
  } ;

std::string CommandLineParams::SeedDatabaseFile ;
std::string CommandLineParams::VerificationFile ;
uint32_t CommandLineParams::TestMachine ; bool CommandLineParams::TestMachinePresent ;
bool CommandLineParams::CheckNFA ;
bool CommandLineParams::TraceOutput ;

int main (int argc, char** argv)
  {
  CommandLineParams::Parse (argc, argv) ;
  TuringMachineReader Reader (CommandLineParams::SeedDatabaseFile.c_str()) ;

  // Open the Decider Verification File
  FiniteAutomataReduction Verifier (CommandLineParams::CheckNFA, CommandLineParams::TraceOutput) ;
  Verifier.fp = fopen (CommandLineParams::VerificationFile.c_str(), "rb") ;
  if (Verifier.fp == 0)
    printf ("File \"%s\" not found\n", CommandLineParams::VerificationFile.c_str()), exit (1) ;

  // First uint in dvf is number of machines
  uint32_t nMachines = Read32 (Verifier.fp) ;
  int LastPercent = -1 ;
  uint8_t MachineSpec[MACHINE_SPEC_SIZE] ;

  if (CommandLineParams::TestMachinePresent)
    {
    // Verify a single machine (for development purposes)
    for ( ; ; )
      {
      Verifier.SeedDatabaseIndex = Read32 (Verifier.fp) ;
      if (Verifier.SeedDatabaseIndex < CommandLineParams::TestMachine)
        {
        Verifier.ReadVerificationInfo() ;
        continue ;
        }
      if (Verifier.SeedDatabaseIndex != CommandLineParams::TestMachine)
        printf ("Machine %d not found\n", CommandLineParams::TestMachine), exit (1) ;
      Reader.Read (Verifier.SeedDatabaseIndex, MachineSpec) ;
      Verifier.ReadVerificationInfo() ;

      Verifier.Verify (MachineSpec) ;

      exit (0) ;
      }
    }

  clock_t Timer = clock() ;

  for (uint32_t Entry = 0 ; Entry < nMachines ; Entry++)
    {
    int Percent = ((Entry + 1) * 100LL) / nMachines ;
    if (Percent != LastPercent)
      {
      printf ("\r%d%%", Percent) ;
      fflush (stdout) ;
      LastPercent = Percent ;
      }

    // Read SeedDatabaseIndex and Tag from dvf
    Verifier.SeedDatabaseIndex = Read32 (Verifier.fp) ;

    // Read the machine spec from the seed database file
    Reader.Read (Verifier.SeedDatabaseIndex, MachineSpec) ;

    // Read the verification info from the file
    Verifier.ReadVerificationInfo() ;

    // Verify it
    Verifier.Verify (MachineSpec) ;
    }

  Timer = clock() - Timer ;

  if (fread (MachineSpec, 1, 1, Verifier.fp) != 0)
    printf ("File too long!\n"), exit (1) ;
  fclose (Verifier.fp) ;

  printf ("\n%d machines verified\n", nMachines) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;

  for (uint32_t i = 0 ; i <= FiniteAutomataReduction::MaxDFA_States ; i++)
    if (Verifier.MachineCount[i]) printf ("%d: %d\n", i, Verifier.MachineCount[i]) ;
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

      case 'X':
        TestMachine = atoi (&argv[0][2]) ;
        TestMachinePresent = true ;
        break ;

      case 'O':
        TraceOutput = true ;
        break ;

      case 'C':
        CheckNFA = true ;
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
VerifyFAR <param> <param>...
  <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
           -V<verification file>  Decider Verification File for decided machines
           -X<test machine>       Machine to test
           -F                     Reconstruct NFA and check it against NFA in dvf
           -O                     Trace output
)*RAW*") ;
  exit (status) ;
  }
