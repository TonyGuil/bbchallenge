// VerifyBouncers <param> <param>...
//   <param>: -N<states>            Machine states (2, 3, 4, 5, or 6)
//            -D<database>          Seed database file (defaults to ../SeedDatabase.bin)
//            -V<verification file> Input file: verification data to be checked
//            -S<space limit>       Max absolute value of tape head
//
// Format of verification info:
//
//   byte BouncerType  -- Unilateral=1, Bilateral=2, Translated=3
//   ubyte nPartitions -- usually 1, but can be up to 4 (so far)
//   ushort nRuns      -- usually 2, but can be up to 156 (so far)
// 
//   uint InitialSteps    -- Steps to reach the start of the Cycle
//   int InitialLeftmost  -- Leftmost cell visited at start of Cycle
//   int InitialRightmost -- Rightmost cell visited at start of Cycle
// 
//   ushort RepeaterCount[nPartitions] -- the Repeater count for each partition
//                                     -- remains constant throughout the cycle
//   TapeDescriptor InitialTape   -- Tape contents and state at start of Cycle
//   RunDescriptor RunList[nRuns] -- Definition of each Run
//
//   uint FinalSteps      -- Steps to reach the end of the Cycle
//   int FinalLeftmost    -- Leftmost cell visited at end of Cycle
//   int FinalRightmost   -- Rightmost cell visited at end of Cycle
//   TapeDescriptor FinalTape  -- Tape contents and state at end of Cycle
// 
// RunDescriptor:
//   ubyte Partition -- Partiton which the Repeaters traverse
//   SegmentTransition RepeaterTransition
//   SegmentTransition WallTransition
// 
// SegmentTransition: -- defines a transition from an initial tape segment to a final tape segment
//   ushort nSteps
//   Segment Initial -- Initial.TapeHead must be strictly contained in Tape
//   Segment Final   -- Final.TapeHead may lie immediately to the left or right of Tape
// 
// Segment: -- a short stretch of tape, with state and tape head
//   ubyte State
//   short TapeHead -- relative to Tape[0]
//   ByteArray Tape
// 
// TapeDescriptor:
//   -- Wall[0] Repeater[0] ... Wall[nPartitions-1] Repeater[nPartitions-1]  Wall[nPartitions]
//   -- For each partition, the number of repetitions of each Repeater remains unchanged
//   -- throughout the Cycle, and is found in the RepeaterCount array in the VerificationInfo
//   ubyte State
//   ubyte TapeHeadWall
//   short TapeHeadOffset -- Offset of tape head relative to Wall[TapeHeadWall]
//   ByteArray Wall[nPartitions + 1]
//   ByteArray Repeater[nPartitions]
// 
// ByteArray:
//   ushort Len
//   ubyte Data[Len]

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <string>

#include "BouncerVerifier.h"
#include "../Params.h"

class CommandLineParams : public VerifierParams
  {
public:
  uint32_t SpaceLimit = 5000 ;
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

  BouncerVerifier Verifier (Params.MachineStates, Params.SpaceLimit) ;
  int LastPercent = -1 ;
  uint8_t MachineSpec[MAX_MACHINE_SPEC_SIZE] ;

  clock_t Timer = clock() ;

  uint32_t nHalters = 0 ;
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
    Reader.Read (MachineIndex, MachineSpec) ;
    Verifier.Initialise (MachineIndex, MachineSpec) ;

    switch (DeciderTag (Read32 (Params.fpVerify)))
      {
      case DeciderTag::NEW_BOUNCER:
        Verifier.Verify (Params.fpVerify) ;
        break ;

      case DeciderTag::BOUNCER:
        printf ("\nDecider tag BOUNCER (6) no longer supported!\n") ;
        exit (1) ;

      case DeciderTag::HALT:
        Verifier.VerifyHalter (Params.fpVerify) ;
        nHalters++ ;
        break ;

      default:
        printf ("\n%d: Unrecognised DeciderTag\n", MachineIndex), exit (1) ;
      }
    }

  Timer = clock() - Timer ;

  if (!CheckEndOfFile (Params.fpVerify)) printf ("File too long!\n"), exit (1) ;
  fclose (Params.fpVerify) ;
  printf ("\n%d Bouncers verified\n", Reader.nMachines - nHalters) ;
  if (nHalters) printf ("%d Halters verified\n", nHalters) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;
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
  printf ("VerifyBouncers <param> <param>...") ;
  CommonParams::PrintHelp() ;
  printf (R"*RAW*(
           -S<space limit>       Max absolute value of tape head
)*RAW*") ;
  exit (status) ;
  }
