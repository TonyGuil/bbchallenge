#pragma once

#include "Bouncer.h"

class BouncerDecider : public Bouncer
  {
public:
  BouncerDecider (uint32_t TimeLimit, uint32_t SpaceLimit, bool TraceOutput)
  : Bouncer (TimeLimit, SpaceLimit, TraceOutput)
    {
    Clone = new TuringMachine (TimeLimit, SpaceLimit) ;

    // Allocate workspace
    RecordLimit = 5000 ; // for now
    RightRecordList = new Record[RecordLimit] ;
    LeftRecordList = new Record[RecordLimit] ;

    ConfigWorkspaceSize = 4 * TimeLimit + WRAPAROUND ; // for now
    ConfigWorkspace = new Config[ConfigWorkspaceSize] ;
    }

  void ThreadFunction (int nMachines, const uint32_t* MachineIndexList,
    const uint8_t* MachineSpecList, uint8_t* VerificationEntryList, uint32_t VerifLength) ;

  bool RunDecider (const uint8_t* MachineSpec, uint8_t* VerificationEntry) ;

  struct Record
    {
    uint32_t StepCount ;
    int TapeHead ;
    Record* Prev ; // Previous record with same state
    } ;
  Record* LeftRecordList ;
  Record* RightRecordList ;
  Record* LatestLeftRecord[NSTATES + 1] ;
  Record* LatestRightRecord[NSTATES + 1] ;
  uint32_t RecordLimit ;

  struct Config
    {
    int TapeHead ;
    uint8_t State ;
    uint8_t Cell ;
    bool operator== (const Config& Conf) const
      {
      return State == Conf.State && Cell == Conf.Cell ;
      }
    bool operator!= (const Config& Conf) const
      {
      return !operator== (Conf) ;
      }
    } ;
  Config* ConfigWorkspace ;
  uint32_t ConfigWorkspaceSize ;

  uint32_t StepCount1 ;
  uint32_t StepCount2 ;

  TuringMachine* Clone ;

  bool DetectRepetition (Record* LatestRecord[], uint8_t State, uint8_t* VerificationEntry) ;
  bool QuadraticProgression (int a1, int a2, int a3, int a4) ;
  void GetRepetitionParams (int a1, int a2, int a3) ;
  bool FindRuns (Config* Run1, Config* Run2) ;

  int CycleShift ; // Change in TapeHead after a whole cycle of runs
  int Cycle1Leftmost, Cycle1Rightmost ;
  int Cycle2Leftmost, Cycle2Rightmost ;
  int InitialLeftmost, InitialRightmost ;
  int FinalLeftmost, FinalRightmost ;

  // For Translated Bouncers: 0, 1, or -1
  int8_t TB_Direction ;
  uint32_t TB_Size ;
  std::vector<uint8_t> TB_Wall ;
  std::vector<uint8_t> TB_Repeater ;
  uint32_t TB_RepeaterCount ;
  void MakeTranslatedBouncerData() ;

  struct RunData
    {
    uint8_t Partition ;
    int8_t Direction ; // +1 is rightward, -1 is leftward
    bool Expanding ;   // Almost always true

    Config* Repeater ;
    int RepeaterShift ;
    uint32_t RepeaterSteps ;

    uint32_t RepeaterPeriod ; // Number of steps in a single repeater
    uint32_t RepeaterCount ;  // Number of repeaters

    Config* Wall ;
    uint32_t WallSteps ;
    } ;
  RunData RunDataArray[MAX_RUNS + 1] ;
  struct PartitionData
    {
    int RepeaterShift ;
    uint32_t RepeaterCount ;
    } ;
  PartitionData PartitionDataArray[MAX_PARTITIONS] ;

  bool FindRepeat (Config* Run1, Config* Run2, RunData& R) ;

  bool AssignPartitions() ;
  bool EqualiseRepeaters() ;
  bool MakeRunDescriptor() ;

  struct RunDescriptor
    {
    uint8_t Partition ;
    int8_t Direction ;
    bool Expanding ;

    Transition RepeaterTransition ;
    Transition WallTransition ;
    } ;

  RunDescriptor RunDescriptorArray[MAX_RUNS] ;

  void ConvertRunData (RunDescriptor& To, const RunData& From) ;
  bool AnalyseTape (const TuringMachine* TM, TapeDescriptor& TD,
    uint32_t Run, int Leftmost, int Rightmost) ;
  bool RemoveGap (TapeDescriptor& TD, const Transition& Tr) ;
  bool TruncateWall (TapeDescriptor& TD, const Transition& Tr) ;

  // Verification data
  uint32_t WriteTapeDescriptor (uint8_t* VerificationEntry, const TuringMachine* TM, const TapeDescriptor& TD) const ;
  uint32_t WriteTransition (uint8_t* VerificationEntry, const Transition& Tr) const ;
  uint32_t WriteSegment (uint8_t* VerificationEntry, const Segment& Seg) const ;

  // Diagnostics
  bool CheckRuns() ;

  void DumpRunData() ;
  void DumpTransitions() ;
  void PrintTransition (const Transition& Tr) ;
  void PrintTape (const TapeDescriptor& Tape) ;
  } ;
