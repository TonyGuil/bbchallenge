// VerifyFAR <param> <param>...
//   <param>: -N<states>            Machine states (2, 3, 4, 5, or 6)
//            -D<database>          Seed database file (defaults to ../SeedDatabase.bin)
//            -V<verification file> Input file: verification data to be checked
//            -F                    Reconstruct NFA and check it against NFA in dvf

#include <time.h>
#include <string>

#include "../bbchallenge.h"
#include "../Params.h"
#include "FAR.h"

//
// Command-line parameters
//

class CommandLineParams : public VerifierParams
  {
public:
  bool CheckNFA ;
  void Parse (int argc, char** argv) ;
  void PrintHelpAndExit [[noreturn]] (int status) ;
  } ;

static CommandLineParams Params ;
static TuringMachineReader Reader ;

int main (int argc, char** argv)
  {
  Params.Parse (argc, argv) ;
  Params.CheckParameters() ;
  Params.OpenFiles() ;

  Reader.SetParams (&Params) ;

  FiniteAutomataReduction Verifier (Params.MachineStates, Params.fpVerify, Params.CheckNFA) ;

  uint8_t MachineSpec[MAX_MACHINE_SPEC_SIZE] ;
  int LastPercent = -1 ;

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

  if (!CheckEndOfFile (Verifier.fp)) printf ("File too long!\n"), exit (1) ;
  fclose (Verifier.fp) ;

  printf ("\n%d machines verified\n", Reader.nMachines) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;

  for (uint32_t i = 0 ; i <= FiniteAutomataReduction::MaxDFA_States ; i++)
    if (Verifier.MachineCount[i]) printf ("%d: %d\n", i, Verifier.MachineCount[i]) ;
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
      case 'F':
        CheckNFA = true ;
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
  printf ("VerifyFAR <param> <param>...\n") ;
  CommonParams::PrintHelp() ;
  printf (R"*RAW*(
           -F                    Reconstruct NFA and check it against NFA in dvf
)*RAW*") ;
  exit (status) ;
  }
