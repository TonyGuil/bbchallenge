#pragma once

#include "bbchallenge.h"

class TuringMachineReader ;

class TuringMachineSpec
  {
public:
  TuringMachineSpec (uint32_t MachineStates) ;
  TuringMachineSpec (const TuringMachineReader& Reader) ;

  struct Transition
    {
    uint8_t Write ;
    uint8_t Move ; // 1 = Left, 0 = Right
    uint8_t Next ;
    } ;

  void Initialise (int Index, const uint8_t* MachineSpec) ;
  void UnpackSpec (Transition* S, const uint8_t* MachineSpec) ;

  uint32_t MachineStates ;
  uint32_t SeedDatabaseIndex ;
  uint32_t MachineSpecSize ;

  Transition TM[MAX_MACHINE_STATES + 1][2] ;
  } ;

class TuringMachine : public TuringMachineSpec
  {
public:
  TuringMachine (uint32_t  MachineStates, uint32_t SpaceLimit) ;
  TuringMachine (const TuringMachineReader& Reader, uint32_t SpaceLimit) ;
  ~TuringMachine()
    {
    delete[] TapeWorkspace ;
    }

  uint8_t* Tape ;
  int TapeHead ;
  uint8_t State ;

  uint32_t SpaceLimit ;
  int Leftmost ;
  int Rightmost ;
  uint64_t StepCount ;
  int8_t RecordBroken ; // +/-1 if the latest step broke a right or left record

  void Initialise (int Index, const uint8_t* MachineSpec)
    {
    TuringMachineSpec::Initialise (Index, MachineSpec) ;
    Reset() ;
    }

  void Reset() ;
  TuringMachine& operator= (const TuringMachine& Src) ;
  StepResult Step() ;

protected:

  uint8_t* TapeWorkspace ;
  } ;
