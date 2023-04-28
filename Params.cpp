#include <stdlib.h>
#include <ctype.h>
#include "Params.h"

bool CommonParams::ParseParam (const char* arg)
  {
  if (arg[0] != '-') return false ;
  switch (toupper (arg[1]))
    {
    case 'D':
      if (arg[2] == 0) printf ("%s: filename expected\n", arg), PrintHelpAndExit (1) ;
      DatabaseFilename = std::string (&arg[2]) ;
      return true ;

    case 'N':
      MachineStates = ParseInt (arg, arg + 2) ;
      return true ;

    case 'V':
      if (arg[2] == 0) printf ("%s: filename expected\n", arg), PrintHelpAndExit (1) ;
      VerificationFilename = std::string (&arg[2]) ;
      return true ;

    default: return false ;
    }
  }

void CommonParams::CheckParameters()
  {
  if (MachineStates < 2 || MachineStates > 6)
    printf ("Invalid MachineStates parameter %d\n", MachineStates), exit (1) ;
  if (MachineStates != 5) BinaryMachineSpecs = false ;
  }

bool DeciderParams::ParseParam (const char* arg)
  {
  if (arg[0] != '-') return false ;

  switch (toupper (arg[1]))
    {
    case 'I':
      if (arg[2] == 0) printf ("%s: filename expected\n", arg), PrintHelpAndExit (1) ;
      InputFilename = std::string (&arg[2]) ;
      return true ;

    case 'U':
      if (arg[2] == 0) printf ("%s: filename expected\n", arg), PrintHelpAndExit (1) ;
      UndecidedFilename = std::string (&arg[2]) ;
      return true ;

    case 'X':
      TestMachine = ParseInt (arg, arg + 2) ;
      TestMachinePresent = true ;
      return true ;

    case 'M':
      if (arg[2] == 0) printf ("Invalid parameter \"%s\"\n", arg), PrintHelpAndExit (1) ;
      MachineSpec = std::string (&arg[2]) ;
      BinaryMachineSpecs = false ;
      return true ;

    case 'L':
      MachineLimit = ParseInt (arg, arg + 2) ;
      MachineLimitPresent = true ;
      return true ;

    case 'H':
      nThreads = ParseInt (arg, arg + 2) ;
      nThreadsPresent = true ;
      return true ;

    case 'O':
      TraceOutput = true ;
      return true ;
    }

  return CommonParams::ParseParam (arg) ;
  }

void DeciderParams::CheckParameters()
  {
  CommonParams::CheckParameters() ;
  if (!MachineSpec.empty() || TestMachinePresent)
    {
    if (!InputFilename.empty())
      {
      printf ("-I parameter ignored\n") ;
      InputFilename = std::string() ;
      }
    if (MachineLimitPresent)
      {
      printf ("-L parameter ignored\n") ;
      MachineLimitPresent = false ;
      }
    }
  if (!MachineSpec.empty())
    {
    if (!VerificationFilename.empty())
      {
      printf ("-V parameter ignored\n") ;
      VerificationFilename = std::string() ;
      }

    switch (MachineSpec.length())
      {
      case 13: MachineStates = 2 ; break ;
      case 20: MachineStates = 3 ; break ;
      case 27: MachineStates = 4 ; break ;
      case 34: MachineStates = 5 ; break ;
      case 41: MachineStates = 6 ; break ;
      default:
        printf ("-M%s: machine spec length invalid\n", MachineSpec.c_str()) ;
        exit (1) ;
      }
    }
  }

void VerifierParams::CheckParameters()
  {
  CommonParams::CheckParameters() ;
  }

uint32_t CommonParams::ParseInt (const char* arg, const char* s)
  {
  if (*s == 0) printf ("%s: integer expected\n", arg), exit (1) ;
  for (int i = 0 ; s[i] ; i++) if (!isdigit (s[i]))
    printf ("%s: invalid integer\n", arg), exit (1) ;
  return atoi (s) ;
  }

void CommonParams::PrintHelp() const
  {
  printf (R"*RAW*(
  <param>: -D<database>          Seed database file (defaults to ../SeedDatabase.bin)
           -N<states>            Machine states (2, 3, 4, 5, or 6))*RAW*") ;
  }

void DeciderParams::PrintHelp() const
  {
  CommonParams::PrintHelp() ;
  printf (R"*RAW*(
           -I<input file>        Input file: list of machines to be analysed (default=all machines)
           -X<test machine>      Machine to test
           -M<machine spec>      Compact machine code (ASCII spec) to test
           -L<machine limit>     Max no. of machines to test
           -V<verification file> Output file: verification data for decided machines
           -U<undecided file>    Output file: remaining undecided machines
           -H<threads>           Number of threads to use
           -O                    Print trace output)*RAW*") ;
  }

void VerifierParams::PrintHelp() const
  {
  CommonParams::PrintHelp() ;
  printf (R"*RAW*(
           -V<verification file> Input file: verification data to be checked)*RAW*") ;
  }
