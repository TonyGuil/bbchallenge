#include "TuringMachine.h"
#include "Params.h"

TuringMachineSpec::TuringMachineSpec (uint32_t  MachineStates)
  : MachineStates (MachineStates)
  {
  MachineSpecSize = 6 * MachineStates ;
  }

TuringMachineSpec::TuringMachineSpec (const TuringMachineReader& Reader)
  : TuringMachineSpec (Reader.Params -> MachineStates)
  {
  }

void TuringMachineSpec::Initialise (int Index, const uint8_t* MachineSpec)
  {
  SeedDatabaseIndex = Index ;
  Transition* S = &TM[1][0] ;
  for (uint32_t i = 0 ; i < 2 * MachineStates ; i++, S++)
    {
    UnpackSpec (S, MachineSpec) ;
    MachineSpec += 3 ;
    }
  }

void TuringMachineSpec::UnpackSpec (Transition* S, const uint8_t* MachineSpec)
  {
  S -> Write = MachineSpec[0] ;
  S -> Move = MachineSpec[1] ;
  S -> Next = MachineSpec[2] ;
  if (S -> Move > 1 || S -> Write > 1 || S -> Next > MachineStates)
    TM_ERROR() ;
  }

TuringMachine::TuringMachine (uint32_t  MachineStates, uint32_t SpaceLimit)
: TuringMachineSpec (MachineStates), SpaceLimit (SpaceLimit)
  {
  memset (TM[0], 0, 2 * sizeof (Transition)) ;
  TapeWorkspace = new uint8_t[2 * SpaceLimit + 1] ;
  TapeWorkspace[0] = TapeWorkspace[2 * SpaceLimit] = TAPE_SENTINEL ;
  Tape = TapeWorkspace + SpaceLimit ;
  }

TuringMachine::TuringMachine (const TuringMachineReader& Reader, uint32_t SpaceLimit)
  : TuringMachine (Reader.Params -> MachineStates, SpaceLimit)
  {
  }

void TuringMachine::Reset()
  {
  memset (Tape - SpaceLimit + 1, 0, 2 * SpaceLimit - 1) ;
  TapeHead = Leftmost = Rightmost = 0 ;
  State = 1 ;
  StepCount = 0 ;
  RecordBroken = 0 ;
  }

TuringMachine& TuringMachine::operator= (const TuringMachine& Src)
  {
  if (MachineStates != Src.MachineStates || SpaceLimit != Src.SpaceLimit)
    printf ("Error 1 in TuringMachine::operator=\n"), exit (1) ;

  SeedDatabaseIndex = Src.SeedDatabaseIndex ;
  TapeHead = Src.TapeHead ;
  State = Src.State ;
  Leftmost = Src.Leftmost ;
  Rightmost = Src.Rightmost ;
  StepCount = Src.StepCount ;
  RecordBroken = Src.RecordBroken ;

  memcpy (TM, Src.TM, sizeof (TM)) ;
  memcpy (TapeWorkspace, Src.TapeWorkspace, 2 * SpaceLimit + 1) ;

  return *this ;
  }

StepResult TuringMachine::Step()
  {
  RecordBroken = 0 ;
  uint8_t Cell = Tape[TapeHead] ;
  if (Cell == TAPE_SENTINEL) return StepResult::OUT_OF_BOUNDS ;
  const Transition& S = TM[State][Cell] ;
  Tape[TapeHead] = S.Write ;
  if (S.Move) // Left
    {
    TapeHead-- ;
    if (TapeHead < Leftmost)
      {
      Leftmost = TapeHead ;
      RecordBroken = -1 ;
      }
    }
  else
    {
    TapeHead++ ;
    if (TapeHead > Rightmost)
      {
      Rightmost = TapeHead ;
      RecordBroken = 1 ;
      }
    }
  State = S.Next ;
  StepCount++ ;
  return State ? StepResult::OK : StepResult::HALT ;
  }
