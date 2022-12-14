// BackwardReasoning <param> <param>...
//
//   <param>: -S<seed database>      Seed database file (defaults to ../SeedDatabase.bin)
//            -I<input file>         Input file: list of machines to be analysed
//            -V<verification file>  Output file: verification data for decided machines
//            -U<undecided file>     Output file: remaining undecided machines
//            -D<depth limit>        Max search depth
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

#include "../bbchallenge.h"

// This Decider can't offer much in the way of verification data. It just saves
// Leftmost, Rightmost, MaxDepth, and nNodes. No verifier program has been written:
#define VERIF_INFO_LENGTH 16

// We need a special value to indicate that the contents of a cell on the tape
// are so far undetermined:
#define TAPE_UNSET 3

//
// Command-line parameters
//

class CommandLineParams
  {
public:
  static std::string SeedDatabaseFile ;
  static std::string InputFile ;
  static std::string VerificationFile ;
  static std::string UndecidedFile ;
  static uint32_t DepthLimit ;    static bool DepthLimitPresent ;
  static void Parse (int argc, char** argv) ;
  static void PrintHelpAndExit [[noreturn]] (int status) ;
  } ;

std::string CommandLineParams::SeedDatabaseFile ;
std::string CommandLineParams::InputFile ;
std::string CommandLineParams::VerificationFile ;
std::string CommandLineParams::UndecidedFile ;
uint32_t CommandLineParams::DepthLimit ;    bool CommandLineParams::DepthLimitPresent ;

//
// BackwardReasoning class
//

class BackwardReasoning
  {
public:
  BackwardReasoning (int DepthLimit, int SpaceLimit)
  : DepthLimit (DepthLimit), SpaceLimit (SpaceLimit)
    {
    // Allocate the tape workspace
    Tape = new uint8_t[2 * SpaceLimit + 1] ;
    Tape[0] = Tape[2 * SpaceLimit] = TAPE_SENTINEL ;
    Tape += SpaceLimit ; // so Tape[0] is in the middle

    // Reserve maximum possible lengths for the backward transition vectors,
    // to avoid having to re-allocate them in the middle of the search (a
    // mini-optimisation)
    for (int i = 0 ; i <= NSTATES ; i++)
      TransitionTable[i].reserve (2 * NSTATES) ;
    }

  // Call Run to analyse a single machine. MachineSpec is in the 30-byte
  // Seed Database format:
  bool Run (const uint8_t* MachineSpec) ;

  uint32_t SeedDatabaseIndex ;
  uint8_t* Tape ;

  // Transition struct contains the parameters of a possible predecessor state
  struct Transition
    {
    uint8_t State ;
    uint8_t Read ;
    uint8_t Write ;
    uint8_t Move ;
    } ;

  // Each state can be reached from a number of predecessor states:
  std::vector<Transition> TransitionTable[NSTATES + 1] ;

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
  CommandLineParams::Parse (argc, argv) ;

  TuringMachineReader Reader (CommandLineParams::SeedDatabaseFile.c_str()) ;

  // fpin contains the list of machines to analyse
  FILE* fpin = fopen (CommandLineParams::InputFile.c_str(), "rb") ;
  if (fpin == NULL) printf ("Can't open input file\n"), exit (1) ;
  if (fseek (fpin, 0, SEEK_END))
    printf ("fseek failed\n"), exit (1) ;
  uint32_t InputFileSize = ftell (fpin) ;
  if (InputFileSize & 3) // Must be a multiple of 4 bytes
    printf ("Invalid input file %s\n", CommandLineParams::InputFile.c_str()), exit (1) ;
  if (fseek (fpin, 0, SEEK_SET))
    printf ("fseek failed\n"), exit (1) ;

  FILE* fpUndecided = 0 ;
  if (!CommandLineParams::UndecidedFile.empty())
    {
    fpUndecided = fopen (CommandLineParams::UndecidedFile.c_str(), "wb") ;
    if (fpUndecided == NULL)
      printf ("Can't open output file \"%s\"\n", CommandLineParams::UndecidedFile.c_str()), exit (1) ;
    }

  FILE* fpVerif = 0 ;
  if (!CommandLineParams::VerificationFile.empty())
    {
    fpVerif = fopen (CommandLineParams::VerificationFile.c_str(), "wb") ;
    if (fpVerif == NULL)
      printf ("Can't open output file \"%s\"\n", CommandLineParams::VerificationFile.c_str()), exit (1) ;
    }

  uint32_t nMachines = InputFileSize >> 2 ;

  // Write dummy dvf header
  Write32 (fpVerif, 0) ;

  clock_t Timer = clock() ;

  uint32_t nDecided = 0 ;
  int LastPercent = -1 ;
  uint8_t MachineSpec[MACHINE_SPEC_SIZE] ;
  uint8_t VerificationEntry[VERIF_ENTRY_LENGTH] ;
  Save32 (VerificationEntry + 4, uint32_t (DeciderTag::BACKWARD_REASONING)) ;
  Save32 (VerificationEntry + 8, VERIF_INFO_LENGTH) ;

  BackwardReasoning Decider (CommandLineParams::DepthLimit, MAX_SPACE) ;
  for (uint32_t Entry = 0 ; Entry < nMachines ; Entry++)
    {
    uint32_t SeedDatabaseIndex = Read32 (fpin) ;
    Reader.Read (SeedDatabaseIndex, MachineSpec) ;
    if (Decider.Run (MachineSpec))
      {
      Save32 (VerificationEntry, SeedDatabaseIndex) ;
      Save32 (VerificationEntry + 12, Decider.Leftmost) ;
      Save32 (VerificationEntry + 16, Decider.Rightmost) ;
      Save32 (VerificationEntry + 20, Decider.MaxDepth) ;
      Save32 (VerificationEntry + 24, Decider.nNodes) ;
      if (fpVerif && fwrite (VerificationEntry, VERIF_ENTRY_LENGTH, 1, fpVerif) != 1)
        printf ("Write error\n"), exit (1) ;
      nDecided++ ;
      }
    else Write32 (fpUndecided, SeedDatabaseIndex) ;

    int Percent = ((Entry + 1) * 100LL) / nMachines ;
    if (Percent != LastPercent)
      {
      LastPercent = Percent ;
      printf ("\r%d%% %d %d", Percent, Entry + 1, nDecided) ;
      }
    }
  printf ("\n") ;

  if (fpUndecided) fclose (fpUndecided) ;

  if (fpVerif)
    {
    // Write the verification file header
    if (fseek (fpVerif, 0 , SEEK_SET))
      printf ("\nfseek failed\n"), exit (1) ;
    Write32 (fpVerif, nDecided) ;
    fclose (fpVerif) ;
    }
  fclose (fpin) ;

  Timer = clock() - Timer ;

  printf ("\nDecided %d out of %d\n", nDecided, nMachines) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;
  }

bool BackwardReasoning::Run (const uint8_t* MachineSpec)
  {
  for (int i = 0 ; i <= NSTATES ; i++) TransitionTable[i].clear() ;

  // Built the backward transition table from the MachineSpec
  for (uint8_t State = 1 ; State <= NSTATES ; State++)
    for (uint8_t Cell = 0 ; Cell <= 1 ; Cell++)
      {
      Transition T ;
      T.State = State ;
      T.Read = Cell ;
      T.Write = *MachineSpec++ ;
      T.Move = *MachineSpec++ ;
      uint8_t Next = *MachineSpec++ ;
      TransitionTable[Next].push_back (T) ;
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
  for (const auto& T : TransitionTable[Config.State])
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
    if (argv[0][0] != '-') printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
    switch (toupper (argv[0][1]))
      {
      case 'S':
        if (argv[0][2] == 0) printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
        SeedDatabaseFile = std::string (&argv[0][2]) ;
        break ;

      case 'I':
        if (argv[0][2] == 0) printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
        InputFile = std::string (&argv[0][2]) ;
        break ;

      case 'V':
        if (argv[0][2] == 0) printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
        VerificationFile = std::string (&argv[0][2]) ;
        break ;

      case 'U':
        if (argv[0][2] == 0) printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
        UndecidedFile = std::string (&argv[0][2]) ;
        break ;

      case 'D':
        DepthLimit = atoi (&argv[0][2]) ;
        DepthLimitPresent = true ;
        break ;

      default:
        printf ("Invalid parameter \"%s\"\n", argv[0]) ;
        PrintHelpAndExit (1) ;
      }
    }

  if (InputFile.empty()) printf ("Input file not specified\n"), PrintHelpAndExit (1) ;
  if (!DepthLimitPresent) printf ("Depth limit not specified\n"), PrintHelpAndExit (1) ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf (R"*RAW*(
BackwardReasoning <param> <param>...
  <param>: -S<seed database>      Seed database file (defaults to ../SeedDatabase.bin)
           -I<input file>         Input file: list of machines to be analysed
           -V<verification file>  Output file: verification data for decided machines
           -U<undecided file>     Output file: remaining undecided machines
           -D<depth limit>        Max search depth
)*RAW*") ;
  exit (status) ;
  }
