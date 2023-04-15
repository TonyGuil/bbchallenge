// BackwardReasoning <param> <param>...
//   <param>: -N<states>            Machine states (5 or 6)
//            -D<database>          Seed database file (defaults to ../SeedDatabase.bin)
//            -V<verification file> Output file: verification data for decided machines
//            -I<input file>        Input file: list of machines to be analysed (default=all machines)
//            -U<undecided file>    Output file: remaining undecided machines
//            -X<test machine>      Machine to test
//            -M<machine spec>      Compact machine code (ASCII spec) to test
//            -L<machine limit>     Max no. of machines to test
//            -H<threads>           Number of threads to use
//            -O                    Print trace output
//            -S<depth limit>        Max search depth
//
// The Backward Reasoning Decider starts from the HALT state and recursively generates 
// all possible predecessor states. If it can determine that all possible states lie
// within a distance MaxDepth of the HALT state, and none of these states is the starting
// state, then there is no way to reach the HALT state from the starting state, and
// the machine can be flagged as non-halting.
//
// In practice we don't need to detect the starting state, because all the machines
// that we analyse are known to run for at least 12289 steps, and we never search that
// deep.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>

#include "../TuringMachine.h"
#include "../Params.h"

// This Decider can't offer much in the way of verification data. It just saves
// Leftmost, Rightmost, MaxDepth, and nNodes. No verifier program has been written:
#define VERIF_INFO_LENGTH 16

// We need a special value to indicate that the contents of a cell on the tape
// are so far undetermined:
#define TAPE_UNSET 3

//
// Command-line parameters
//

class CommandLineParams : public DeciderParams
  {
public:
  uint32_t DepthLimit ; bool DepthLimitPresent = false ;
  void Parse (int argc, char** argv) ;
  void PrintHelpAndExit [[noreturn]] (int status) ;
  } ;

static CommandLineParams Params ;

//
// BackwardReasoning class
//

class BackwardReasoning : public TuringMachineSpec
  {
public:
  BackwardReasoning (int MachineStates, int DepthLimit, int SpaceLimit)
  : TuringMachineSpec (MachineStates)
  , DepthLimit (DepthLimit)
  , SpaceLimit (SpaceLimit)
    {
    // Allocate the tape workspace
    Tape = new uint8_t[2 * SpaceLimit + 1] ;
    Tape[0] = Tape[2 * SpaceLimit] = TAPE_SENTINEL ;
    Tape += SpaceLimit ; // so Tape[0] is in the middle

    // Reserve maximum possible lengths for the predecessor vectors,
    // to avoid having to re-allocate them in the middle of the search (a
    // mini-optimisation)
    for (int i = 0 ; i <= MachineStates ; i++)
      PredecessorTable[i].reserve (2 * MachineStates) ;
    }

  // Call Run to analyse a single machine
  bool Run (const uint8_t* MachineSpec) ;

  uint8_t* Tape ;

  // Predecessor struct contains the parameters of a possible predecessor state
  struct Predecessor : public Transition
    {
    uint8_t State ;
    uint8_t Read ;
    } ;

  // Each state can be reached from a number of predecessor states:
  std::vector<Predecessor> PredecessorTable[MAX_MACHINE_STATES + 1] ;

  // The Configuration struct doesn't need to contain the tape contents,
  // because we update the tape dynamically as we recurse
  struct Configuration
    {
    uint8_t State ;
    int16_t TapeHead ;
    } ;

  // Call Recurse with Depth = 0 to start the search
  bool Recurse (uint32_t Depth, const Configuration& Config) ;

  uint32_t DepthLimit ;
  uint32_t SpaceLimit ;

  // Stats
  int Leftmost, Rightmost ;
  uint32_t MaxDepth ;
  uint32_t nNodes ;
  } ;

int main (int argc, char** argv)
  {
  Params.Parse (argc, argv) ;

  Params.CheckParameters() ;
  Params.OpenFiles() ;

  TuringMachineReader Reader (&Params) ;

  // Write dummy dvf header
  Write32 (Params.fpVerify, 0) ;

  clock_t Timer = clock() ;

  uint32_t nDecided = 0 ;
  int LastPercent = -1 ;
  uint8_t MachineSpec[MAX_MACHINE_SPEC_SIZE] ;
  uint8_t VerificationEntry[VERIF_ENTRY_LENGTH] ;
  Save32 (VerificationEntry + 4, uint32_t (DeciderTag::BACKWARD_REASONING)) ;
  Save32 (VerificationEntry + 8, VERIF_INFO_LENGTH) ;

  BackwardReasoning Decider (Params.MachineStates, Params.DepthLimit, MAX_SPACE) ;
  for (uint32_t Entry = 0 ; Entry < Reader.nMachines ; Entry++)
    {
    uint32_t MachineIndex = Reader.Next (MachineSpec) ;
    if (Decider.Run (MachineSpec))
      {
      Save32 (VerificationEntry, MachineIndex) ;
      Save32 (VerificationEntry + 12, Decider.Leftmost) ;
      Save32 (VerificationEntry + 16, Decider.Rightmost) ;
      Save32 (VerificationEntry + 20, Decider.MaxDepth) ;
      Save32 (VerificationEntry + 24, Decider.nNodes) ;
      if (Params.fpVerify && fwrite (VerificationEntry, VERIF_ENTRY_LENGTH, 1, Params.fpVerify) != 1)
        printf ("Write error\n"), exit (1) ;
      nDecided++ ;
      }
    else Write32 (Params.fpUndecided, MachineIndex) ;

    int Percent = ((Entry + 1) * 100LL) / Reader.nMachines ;
    if (Percent != LastPercent)
      {
      LastPercent = Percent ;
      printf ("\r%d%% %d %d", Percent, Entry + 1, nDecided) ;
      fflush (stdout) ;
      }
    }
  printf ("\n") ;

  if (Params.fpUndecided) fclose (Params.fpUndecided) ;

  if (Params.fpVerify)
    {
    // Write the verification file header
    if (fseek (Params.fpVerify, 0 , SEEK_SET))
      printf ("\nfseek failed\n"), exit (1) ;
    Write32 (Params.fpVerify, nDecided) ;
    fclose (Params.fpVerify) ;
    }
  if (Params.fpInput) fclose (Params.fpInput) ;

  Timer = clock() - Timer ;

  printf ("\nDecided %d out of %d\n", nDecided, Reader.nMachines) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;
  }

bool BackwardReasoning::Run (const uint8_t* MachineSpec)
  {
  for (uint32_t i = 0 ; i <= MachineStates ; i++) PredecessorTable[i].clear() ;

  // Built the backward transition table from the MachineSpec
  for (uint8_t State = 1 ; State <= MachineStates ; State++)
    {
    for (uint8_t Cell = 0 ; Cell <= 1 ; Cell++)
      {
      Predecessor T ;
      UnpackSpec (&T, MachineSpec) ;
      T.State = State ;
      T.Read = Cell ;

      PredecessorTable[T.Next].push_back (T) ;

      MachineSpec += 3 ;
      }
    }

  // Start in state 0 with unspecified tape
  memset (Tape - SpaceLimit + 1, TAPE_UNSET, 2 * SpaceLimit - 1) ;
  Configuration StartConfig ;
  StartConfig.State = 0 ;
  StartConfig.TapeHead = 0 ;

  MaxDepth = nNodes = 0 ;
  Leftmost = Rightmost = 0 ;

  return Recurse (0, StartConfig) ;
  }

bool BackwardReasoning::Recurse (uint32_t Depth, const Configuration& Config)
  {
  if (Depth == DepthLimit) return false ; // Search too deep, no decision possible

  nNodes++ ;
  if (Depth > MaxDepth) MaxDepth = Depth ;

  Configuration PrevConfig ;
  for (const auto& T : PredecessorTable[Config.State])
    {
    // Update the tape head
    if (T.Move)
      {
      PrevConfig.TapeHead = Config.TapeHead + 1 ;
      if (PrevConfig.TapeHead > Rightmost) Rightmost = PrevConfig.TapeHead ;
      }
    else
      {
      PrevConfig.TapeHead = Config.TapeHead - 1 ;
      if (PrevConfig.TapeHead < Leftmost) Leftmost = PrevConfig.TapeHead ;
      }

    uint8_t Cell = Tape[PrevConfig.TapeHead] ;
    switch (Cell)
      {
      case TAPE_SENTINEL: // Tape bounds exceeded (if this happens, it's a bug)
        printf ("Tape bounds exceeded!\n") ;
        exit (0) ;

      case TAPE_UNSET: // New tape cell reached, so just write the expected value
        Tape[PrevConfig.TapeHead] = T.Read ;
        break ;

      default:
        if (Tape[PrevConfig.TapeHead] != T.Write)
          {
          // Clash with required tape cell value, so this is an impossible path
          Tape[PrevConfig.TapeHead] = Cell ; // Restore the previous value
          continue ;
          }

        // Update the tape with the value that it had to contain to reach this state
        Tape[PrevConfig.TapeHead] = T.Read ;
        break ;
      }

    // Perform a backwards step and search deeper
    PrevConfig.State = T.State ;
    if (!Recurse (Depth + 1, PrevConfig)) return false ;
    Tape[PrevConfig.TapeHead] = Cell ;
    }

  // No search returned false, i.e. all searches terminated at a finite depth.
  // So we can't reach this state from the starting position:
  return true ;
  }

void CommandLineParams::Parse (int argc, char** argv)
  {
  if (argc == 1) PrintHelpAndExit (0) ;

  for (argc--, argv++ ; argc ; argc--, argv++)
    {
    if (DeciderParams::ParseParam (argv[0])) continue ;
    if (argv[0][0] != '-') printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
    switch (toupper (argv[0][1]))
      {
      case 'S':
        DepthLimit = atoi (&argv[0][2]) ;
        DepthLimitPresent = true ;
        break ;

      default:
        printf ("Invalid parameter \"%s\"\n", argv[0]) ;
        PrintHelpAndExit (1) ;
      }
    }

  if (!DepthLimitPresent) printf ("Depth limit not specified\n"), PrintHelpAndExit (1) ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf ("BackwardReasoning <param> <param>...") ;
  DeciderParams::PrintHelp() ;
  printf (R"*RAW*(
           -S<depth limit>       Max search depth
)*RAW*") ;
  exit (status) ;
  }
