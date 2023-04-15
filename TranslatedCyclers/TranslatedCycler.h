#pragma once

#include <limits.h>
#include <vector>
#include "../TuringMachine.h"

// Decider-specific Verification Data:
#define VERIF_INFO_LENGTH 32

class TranslatedCycler : public TuringMachine
  {
public:
  TranslatedCycler (uint32_t MachineStates, uint32_t TimeLimit,
    uint32_t SpaceLimit, bool TraceOutput)
  : TuringMachine (MachineStates, SpaceLimit)
  , TimeLimit (TimeLimit)
  , TraceOutput (TraceOutput)
    {
    Clone = new TuringMachine (MachineStates, SpaceLimit) ;

    // Allocate workspace
    RecordLimit = 50000 ; // for now
    RightRecordList = new Record[RecordLimit] ;
    LeftRecordList = new Record[RecordLimit] ;

    MinStat = INT_MAX ;
    MaxStat = INT_MIN ;
    }

  void ThreadFunction (int nMachines, const uint32_t* MachineIndexList,
    const uint8_t* MachineSpecList, uint8_t* VerificationEntryList) ;

  bool Run (const uint8_t* MachineSpec, uint8_t* VerificationEntry) ;

  uint32_t TimeLimit ;

  struct Record
    {
    uint32_t StepCount ;
    int TapeHead ;
    Record* Prev ; // Previous record with same state
    } ;
  Record* LeftRecordList ;
  Record* RightRecordList ;
  Record* LatestLeftRecord[MAX_MACHINE_STATES + 1] ;
  Record* LatestRightRecord[MAX_MACHINE_STATES + 1] ;
  uint32_t RecordLimit ;

  TuringMachine* Clone ;

  bool DetectRepetition (Record* LatestRecord[], uint8_t State, uint8_t* VerificationEntry) ;

  // Whatever we may want to know from time to time:
  int MaxStat ; uint32_t MaxStatMachine ;
  int MinStat ; uint32_t MinStatMachine ;

  bool TraceOutput ;
  } ;
