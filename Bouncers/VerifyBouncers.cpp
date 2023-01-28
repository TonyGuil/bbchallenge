// VerifyBouncers.cpp: verification program for Bouncers decider
//
// To invoke:
//
//   VerifyBouncers <param> <param>...
//     <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
//              -V<verification file>  Output file: verification data for decided machines
//              -S<space limit>        Max absolute value of tape head
//
// Format of verification info:
//
// byte BouncerType
// uint nSteps
// ubyte nPartitions
// ubyte nRuns
// ushort RepeaterCount[nPartitions]
// TapeDescriptor InitialTape
// RunDescriptor RunList[nRuns]
// 
// RunDescriptor:
//   ubyte RepeaterCount
//   Transition RepeaterTransition -- repeated RepeaterCount times
//   TapeDescriptor TD0
//   Transition WallTransition
//   TapeDescriptor TD1
// 
// Transition:
//   Segment Initial
//   Segment Final
// 
// Segment:
//   ubyte State
//   short TapeHead -- relative to Tape[0]
//   ubytearray Tape
// 
// TapeDescriptor:
//   int Leftmost
//   ubyte State
//   ubyte TapeHeadWall
//   short TapeHeadOffset
//   ubytearray Wall[nPartitions + 1]
//   ubytearray Repeater[nPartitions]
// 
// ubytearray:
//   ushort Len
//   ubyte Data[Len]

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <string>

#include "BouncerVerifier.h"

#define VERIF_INFO_LENGTH 32

class CommandLineParams
  {
public:
  static std::string SeedDatabaseFile ;
  static std::string VerificationFile ;
  static uint32_t SpaceLimit ;
  static void Parse (int argc, char** argv) ;
  static void PrintHelpAndExit [[noreturn]] (int status) ;
  } ;

std::string CommandLineParams::SeedDatabaseFile ;
std::string CommandLineParams::VerificationFile ;
uint32_t CommandLineParams::SpaceLimit = 5000 ;

int main (int argc, char** argv)
  {
  CommandLineParams::Parse (argc, argv) ;
  TuringMachineReader Reader (CommandLineParams::SeedDatabaseFile.c_str()) ;

  BouncerVerifier Verifier (CommandLineParams::SpaceLimit) ;
  Verifier.fp = fopen (CommandLineParams::VerificationFile.c_str(), "rb") ;
  if (Verifier.fp == 0)
    printf ("File \"%s\" not found\n", CommandLineParams::VerificationFile.c_str()), exit (1) ;

  uint32_t nEntries = Read32 (Verifier.fp) ;
  int LastPercent = -1 ;
  uint8_t MachineSpec[MACHINE_SPEC_SIZE] ;

  clock_t Timer = clock() ;

  for (uint32_t Entry = 0 ; Entry < nEntries ; Entry++)
    {
    int Percent = ((Entry + 1) * 100LL) / nEntries ;
    if (Percent != LastPercent)
      {
      printf ("\r%d%%", Percent) ;
      fflush (stdout) ;
      LastPercent = Percent ;
      }

    uint32_t SeedDatabaseIndex = Read32 (Verifier.fp) ;
    if (DeciderTag (Read32 (Verifier.fp)) != DeciderTag::BOUNCER)
      printf ("\n%d: Unrecognised DeciderTag\n", SeedDatabaseIndex), exit (1) ;

    Reader.Read (SeedDatabaseIndex, MachineSpec) ;
    Verifier.Verify (SeedDatabaseIndex, MachineSpec, Verifier.fp) ;
    }

  Timer = clock() - Timer ;

  if (fread (MachineSpec, 1, 1, Verifier.fp) != 0)
    printf ("File too long!\n"), exit (1) ;
  fclose (Verifier.fp) ;
  printf ("\n%d Bouncers verified\n", nEntries) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;
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

      case 'S':
        SpaceLimit = atoi (&argv[0][2]) ;
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
VerifyBouncers <param> <param>...
  <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
           -V<verification file>  Decider Verification File for decided machines
           -S<space limit>        Max absolute value of tape head
)*RAW*") ;
  exit (status) ;
  }
