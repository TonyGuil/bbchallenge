#include "BouncerDecider.h"

static uint32_t LCM (uint32_t x, uint32_t y) ;
static uint32_t GCD (uint32_t x, uint32_t y) ;

bool BouncerDecider::RunDecider (const uint8_t* MachineSpec, uint8_t* VerificationEntry)
  {
  Initialise (SeedDatabaseIndex, MachineSpec) ;
  if (VerificationEntry)
    Save32 (VerificationEntry + 8, 0) ; // InfoLength = 0 for now

  uint32_t nLeftRecords = 0 ;
  uint32_t nRightRecords = 0 ;
  memset (LatestLeftRecord, 0, sizeof (LatestLeftRecord)) ;
  memset (LatestRightRecord, 0, sizeof (LatestRightRecord)) ;

  // Give the machine time to settle down
  for (uint32_t i = 0 ; i < 1000 ; i++)
    if (Step() != StepResult::OK) return false ;

  Type = BouncerType::Unknown ;

  while (StepCount < TimeLimit)
    {
    if (RecordBroken == 1)
      {
      if (Tape[TapeHead] == TAPE_SENTINEL) return false ;
      if (nRightRecords == RecordLimit) return false ;
      RightRecordList[nRightRecords].StepCount = StepCount ;
      RightRecordList[nRightRecords].TapeHead = TapeHead ;
      RightRecordList[nRightRecords].Prev = LatestRightRecord[State] ; ;
      LatestRightRecord[State] = &RightRecordList[nRightRecords] ;
      nRightRecords++ ;
      if (DetectRepetition (LatestRightRecord[State], State, VerificationEntry))
        {
        if (TraceOutput) printf ("%d\n", SeedDatabaseIndex) ;
        return true ;
        }
      }
    else if (RecordBroken == -1)
      {
      if (Tape[TapeHead] == TAPE_SENTINEL) return false ;
      if (nLeftRecords == RecordLimit) return false ;
      LeftRecordList[nLeftRecords].StepCount = StepCount ;
      LeftRecordList[nLeftRecords].TapeHead = TapeHead ;
      LeftRecordList[nLeftRecords].Prev = LatestLeftRecord[State] ; ;
      LatestLeftRecord[State] = &LeftRecordList[nLeftRecords] ;
      nLeftRecords++ ;
      if (DetectRepetition (LatestLeftRecord[State], State, VerificationEntry))
        {
        if (TraceOutput) printf ("%d\n", SeedDatabaseIndex) ;
        return true ;
        }
      }

    switch (Step())
      {
      case StepResult::OK: break ;
      case StepResult::OUT_OF_BOUNDS: return false ;
      case StepResult::HALT:
        // This has been seen in BB(6), so we handle it gracefully
        Save32 (VerificationEntry, SeedDatabaseIndex) ;
        Save32 (VerificationEntry + 4, (uint32_t)DeciderTag::HALT) ;
        Save32 (VerificationEntry + 8, 4) ; // Info length
        Save32 (VerificationEntry + 12, StepCount) ;
        Type = BouncerType::Unknown ;
        nHalters++ ;
        return true ;
      }
    }

  if (Type == BouncerType::Bell) nBells++ ;

  return false ;
  }

bool BouncerDecider::DetectRepetition (const Record* LatestRecord, uint8_t State, uint8_t* VerificationEntry)
  {
  #define BACKWARD_SCAN_LENGTH 1000
  const Record* Workspace[4 * BACKWARD_SCAN_LENGTH] ;

  for (int ScanLength = 1 ; ScanLength <= BACKWARD_SCAN_LENGTH ; ScanLength++)
    {
    // Retrieve four more Records
    for (int j = 0 ; j < 4 ; j++)
      {
      if (LatestRecord == 0) return false ;
      Workspace[4 * (ScanLength - 1) + j] = LatestRecord ;
      LatestRecord = LatestRecord -> Prev ;
      }

    // Check that the tape heads are in arithmetic progression
    CycleShift = Workspace[0] -> TapeHead - Workspace[ScanLength] -> TapeHead ;
    if (Workspace[ScanLength] -> TapeHead - Workspace[2 * ScanLength] -> TapeHead != CycleShift
      || Workspace[2 * ScanLength] -> TapeHead - Workspace[3 * ScanLength] -> TapeHead != CycleShift)
        continue ;

    // Check that the step counts are in quadratic progrssion
    if (!QuadraticProgression (Workspace[0] -> StepCount, Workspace[ScanLength] -> StepCount,
      Workspace[2 * ScanLength] -> StepCount, Workspace[3 * ScanLength] -> StepCount))
        continue ;

    GetRepetitionParams (Workspace[2 * ScanLength] -> StepCount,
      Workspace[ScanLength] -> StepCount, Workspace[0] -> StepCount) ;
    if (StepCount1 + StepCount2 >= ConfigWorkspaceSize)
      return false ;
    *Clone = *this ;

    Clone -> Leftmost = Clone -> Rightmost = Clone -> TapeHead ;
    Config* Cycle1 = ConfigWorkspace ;
    for (uint32_t i = 0 ; i < StepCount1 ; i++)
      {
      Cycle1[i].TapeHead = Clone -> TapeHead ;
      Cycle1[i].State = Clone -> State ;
      Cycle1[i].Cell = Clone -> Tape[Clone -> TapeHead] ;
      if (Clone -> Step() != StepResult::OK) return false ;
      }
    if (RecordBroken == 0) return false ;
    if (Clone -> State != State || Clone -> Tape[Clone -> TapeHead] != Tape[TapeHead])
      continue ;
    if (Clone -> TapeHead != TapeHead + CycleShift)
      continue ;

    Cycle1Leftmost = Clone -> Leftmost ;
    Cycle1Rightmost = Clone -> Rightmost ;

    Clone -> Leftmost = Clone -> Rightmost = Clone -> TapeHead ;
    Config* Cycle2 = ConfigWorkspace + StepCount1 ;
    for (uint32_t i = 0 ; i < StepCount2 ; i++)
      {
      Cycle2[i].TapeHead = Clone -> TapeHead ;
      Cycle2[i].State = Clone -> State ;
      Cycle2[i].Cell = Clone -> Tape[Clone -> TapeHead] ;
      if (Clone -> Step() != StepResult::OK) return false ;
      }
    if (RecordBroken == 0) return false ;
    if (Clone -> State != State || Clone -> Tape[Clone -> TapeHead] != Tape[TapeHead])
      continue ;
    if (Clone -> TapeHead != TapeHead + 2 * CycleShift)
      continue ;

    Cycle2Leftmost = Clone -> Leftmost ;
    Cycle2Rightmost = Clone -> Rightmost ;

    if (Cycle1Leftmost == Cycle2Leftmost || Cycle1Rightmost == Cycle2Rightmost)
      Type = BouncerType::Unilateral ;
    else if (Cycle2Leftmost < Cycle1Leftmost && Cycle2Rightmost > Cycle1Rightmost)
      Type = BouncerType::Bilateral ;
    else Type = BouncerType::Translated ;
    int LeftmostShift = Cycle1Leftmost - Cycle2Leftmost ;
    int RightmostShift = Cycle2Rightmost - Cycle1Rightmost ;

    for (uint32_t i = 0 ; i < WRAPAROUND ; i++)
      {
      Cycle2[StepCount2 + i].TapeHead = Clone -> TapeHead ;
      Cycle2[StepCount2 + i].State = Clone -> State ;
      Cycle2[StepCount2 + i].Cell = Clone -> Tape[Clone -> TapeHead] ;
      if (Clone -> Step() != StepResult::OK) return false ;
      }

    if (!FindRuns (Cycle1, Cycle2)) continue ;

    if (Type == BouncerType::Bilateral &&
      (Cycle1Leftmost > Leftmost || Cycle1Rightmost < Rightmost))
        {
        // Mark it provisionally as a Bell (it may turn out not to be after
        // a deeper search; see e.g. #1653511)
        Type = BouncerType::Bell ;
        return false ;
        }

    if (RightmostShift < 0) TB_Direction = -1 ;
    else if (LeftmostShift < 0) TB_Direction = 1 ;
    else TB_Direction = 0 ;

    if (!AssignPartitions()) continue ;
    if (!EqualiseRepeaters()) continue ;
    if (!MakeRunDescriptors()) continue ;
    if (!CheckRuns()) TM_ERROR() ;

    if (TraceOutput)
      {
      DumpRunData() ;
      DumpTransitions() ;
      }

    if (BuildVerificationData (VerificationEntry)) return true ;
    }

  return false ;
  }

bool BouncerDecider::BuildVerificationData (uint8_t* VerificationEntry)
  {
  uint32_t n = RunDataArray[0].Repeater - ConfigWorkspace ;
  for (uint32_t i = 0 ; i < n ; i++)
    if (Step() != StepResult::OK) TM_ERROR() ;
  if (TapeHead != RunDataArray[0].Repeater[0].TapeHead) TM_ERROR() ;

  int InitialLeftmost = Leftmost ;
  int InitialRightmost = Rightmost ;

  // Recalculate Cycle1Leftmost and Cycle1Rightmost
  Config* C = RunDataArray[0].Repeater - StepCount1 ;
  Cycle1Leftmost = Cycle1Rightmost = C++ -> TapeHead ;
  for (uint32_t i = 1 ; i <= StepCount1 ; i++)
    {
    int TapeHead = C++ -> TapeHead ;
    if (TapeHead < Cycle1Leftmost) Cycle1Leftmost = TapeHead ;
    if (TapeHead > Cycle1Rightmost) Cycle1Rightmost = TapeHead ;
    }
  InitialLeftmost = std::min (Cycle1Leftmost, InitialLeftmost) ;
  InitialRightmost = std::max (Cycle1Rightmost, InitialRightmost) ;

  // Recalculate Cycle2Leftmost and Cycle2Rightmost
  C = RunDataArray[0].Repeater ;
  Cycle2Leftmost = Cycle2Rightmost = C++ -> TapeHead ;
  for (uint32_t i = 1 ; i <= StepCount2 ; i++)
    {
    int TapeHead = C++ -> TapeHead ;
    if (TapeHead < Cycle2Leftmost) Cycle2Leftmost = TapeHead ;
    if (TapeHead > Cycle2Rightmost) Cycle2Rightmost = TapeHead ;
    }

  if (!MakeTranslatedBouncerData()) return false ;
  GetMaxWallExtents() ;

  int TapeLeftmost = std::min (Leftmost, Cycle2Leftmost) ;
  int TapeRightmost = std::max (Rightmost, Cycle2Rightmost) ;

  // If AnalyseTape finds a repeater sequence that is too short,
  // it simply decreases RepeaterCount and requests a retry:
  int Retries = 0 ;

TryAgain:

  if (Retries >= 3) return false ;
  if (Retries)
    {
    // After changing RepeaterCount, we must recompute the RunDataTransitions
    if (!MakeRunDescriptors()) TM_ERROR() ;
    if (!CheckRuns()) TM_ERROR() ;
    GetMaxWallExtents() ;
    if (TraceOutput)
      {
      printf ("Retrying\n") ;
      DumpRunData() ;
      DumpTransitions() ;
      }
    }

  Retries++ ;

  *Clone = *this ;
  uint32_t Wall = RunDataArray[0].Partition ;
  if (RunDataArray[0].Direction == -1) Wall++ ;
  InitialTape.Leftmost = TapeLeftmost ;
  InitialTape.Rightmost = TapeRightmost ;
  switch (AnalyseTape (Clone, InitialTape, Wall, RunDataArray[0].Partition,
    RunDescriptorArray[0].RepeaterTransition, Clone -> Leftmost, Clone -> Rightmost))
      {
      case ORA_Result::OK: break ;
      case ORA_Result::RETRY: goto TryAgain ;
      case ORA_Result::ABORT: return false ;
      }

  CheckTape (Clone, InitialTape) ;
  if (RemoveGap (InitialTape, RunDescriptorArray[0].RepeaterTransition))
    CheckTape (Clone, InitialTape) ;
  if (TruncateWall (InitialTape, RunDescriptorArray[0].RepeaterTransition))
    CheckTape (Clone, InitialTape) ;

  uint32_t pVerif = 0 ;
  uint32_t pFinal = 0 ;

  TapeDescriptor TD (this) ;
  RunDataTransitions RDT ;

  TuringMachine TM (MachineStates, SpaceLimit) ;
  int WallOffset = 0 ;
  uint32_t FinalAdjustment = 0 ;
  for ( ; ; )
    {
    TM = *Clone ;
    TD = InitialTape ;
    CheckTape (&TM, TD) ;
    Segment First, Prev ;
    uint32_t StepTarget = 0 ;
    uint32_t i ; for (i = 0 ; i < nRuns ; i++)
      {
      ORA_Result res = ConstructTransitions (InitialTape, i, TD, RDT) ;
      if (res == ORA_Result::RETRY) break ;
      if (res == ORA_Result::ABORT) return false ;
  
      if (i == 0) First = RDT.RepeaterTransition.Initial ;
      else CheckFollowOn (RDT.WallTransition.Initial, Prev) ;
      CheckFollowOn (RDT.RepeaterTransition.Initial, RDT.WallTransition.Final) ;
      CheckFollowOn (RDT.RepeaterTransition.Final, RDT.RepeaterTransition.Initial) ;
      CheckTransitions (InitialTape, i, TD, RDT) ;
  
      for (uint32_t j = 0 ; j < RDT.WallTransition.nSteps ; j++)
        if (TM.Step() != StepResult::OK) TM_ERROR() ;
      if (i == 0)
        {
        if (TM.Leftmost < InitialLeftmost)
          {
          ExpandTapeLeftward (TD, InitialLeftmost - TM.Leftmost) ;
          ExpandTapeLeftward (InitialTape, InitialLeftmost - TM.Leftmost) ;
          InitialLeftmost = TM.Leftmost ;
          }
        if (TM.Rightmost > InitialRightmost)
          {
          ExpandTapeRightward (TD, TM.Rightmost - InitialRightmost) ;
          ExpandTapeRightward (InitialTape, TM.Rightmost - InitialRightmost) ;
          InitialRightmost = TM.Rightmost ;
          }
        *Clone = TM ;
        StepTarget = TM.StepCount + StepCount2 ;
        if (VerificationEntry)
          {
          pVerif = 12 ; // Skip MachineIndex, DeciderTag, VerificationLength
          VerificationEntry[pVerif++] = (uint8_t)Type ;
          VerificationEntry[pVerif++] = nPartitions ;
          pVerif += Save16 (VerificationEntry + pVerif, nRuns) ;
          pVerif += Save32 (VerificationEntry + pVerif, TM.StepCount) ;
          pVerif += Save32 (VerificationEntry + pVerif, TM.Leftmost) ;
          pVerif += Save32 (VerificationEntry + pVerif, TM.Rightmost) ;

          // Save space for FinalSteps, FinalLeftmost, FinalRightmost
          pFinal = pVerif ;
          pVerif += 12 ;

          for (uint32_t j = 0 ; j < nPartitions ; j++)
            pVerif += Save16 (VerificationEntry + pVerif, InitialTape.RepeaterCount[j]) ;
          pVerif += WriteTapeDescriptor (VerificationEntry + pVerif, &TM, InitialTape) ;
          }
        }
      uint32_t RepeaterCount = TD.RepeaterCount[RDT.Partition] ;
      for (uint32_t j = 0 ; j < RepeaterCount * RDT.RepeaterTransition.nSteps ; j++)
        if (TM.Step() != StepResult::OK) TM_ERROR() ;
      CheckTape (&TM, TD) ;

      if (VerificationEntry)
        {
        if (i) pVerif += WriteTransition (VerificationEntry + pVerif, RDT.WallTransition) ;
        VerificationEntry[pVerif++] = RDT.Partition ;
        pVerif += WriteTransition (VerificationEntry + pVerif, RDT.RepeaterTransition) ;
        }

      Prev = RDT.RepeaterTransition.Final ;
      }

    if (i == nRuns)
      {
      if (TM.StepCount > StepTarget) return false ;

      int WallLeftmost = TM.TapeHead - TD.TapeHeadOffset ;
      int WallRightmost = WallLeftmost + TD.Wall[TD.TapeHeadWall].size() - 1 ;

      // Execute the last few steps to get back to the start
      WallOffset = TM.TapeHead ;
      RDT.WallTransition.nSteps = StepTarget - TM.StepCount ;
      int AdjustmentLeftmost = TM.TapeHead ;
      int AdjustmentRightmost = TM.TapeHead ;
      while (TM.StepCount < StepTarget)
        {
        if (TM.TapeHead < AdjustmentLeftmost) AdjustmentLeftmost = TM.TapeHead ;
        if (TM.TapeHead > AdjustmentRightmost) AdjustmentRightmost = TM.TapeHead ;
        if (TM.Step() != StepResult::OK) TM_ERROR() ;
        }

      if (TD.TapeHeadWall == 0)
        {
        uint32_t Stride = TD.Repeater[0].size() ;
        if (AdjustmentRightmost > WallRightmost)
          {
          FinalAdjustment = (AdjustmentRightmost - WallRightmost + Stride - 1) / Stride ;
          for (uint32_t j = 0 ; j < FinalAdjustment ; j++)
            {
            TD.Wall[0] += TD.Repeater[0] ;
            if (TD.Wall[1].size() < Stride) return false ;
            if (!TD.Wall[1].starts_with (TD.Repeater[0])) return false ;
            TD.Wall[1].erase (0, Stride) ;
            }
          }
        }
      else
        {
        uint32_t Stride = TD.Repeater[nPartitions - 1].size() ;
        if (AdjustmentLeftmost < WallLeftmost)
          {
          FinalAdjustment = (WallLeftmost - AdjustmentLeftmost + Stride - 1) / Stride ;
          for (uint32_t j = 0 ; j < FinalAdjustment ; j++)
            {
            TD.Wall[nPartitions].insert (0, TD.Repeater[nPartitions - 1]) ;
            if (TD.Wall[nPartitions - 1].size() < Stride) return false ;
            if (!TD.Wall[nPartitions - 1].ends_with (TD.Repeater[nPartitions - 1])) return false ;
            TD.Wall[nPartitions - 1].resize (TD.Wall[nPartitions - 1].size() - Stride) ;
            TD.TapeHeadOffset += Stride ;
            }
          }
        }
      break ;
      }
    }

  if (TM.Leftmost < TD.Leftmost)
    {
    ExpandTapeLeftward (TD, TD.Leftmost - TM.Leftmost) ;
    ExpandTapeLeftward (InitialTape, InitialTape.Leftmost - TM.Leftmost) ;
    }
  if (TM.Rightmost > TD.Rightmost)
    {
    ExpandTapeRightward (TD, TM.Rightmost - TD.Rightmost) ;
    ExpandTapeRightward (InitialTape, TM.Rightmost - InitialTape.Rightmost) ;
    }
  WallOffset -= TD.TapeHeadOffset ;

  RDT.WallTransition.Initial.State = TD.State ;
  RDT.WallTransition.Initial.Tape = TD.Wall[TD.TapeHeadWall] ;
  RDT.WallTransition.Initial.TapeHead = TD.TapeHeadOffset ;

  memcpy (&TD.Wall[TD.TapeHeadWall][0], TM.Tape + WallOffset,
    TD.Wall[TD.TapeHeadWall].size()) ;
  TD.State = RDT.WallTransition.Final.State = TM.State ;
  RDT.WallTransition.Final.Tape = TD.Wall[TD.TapeHeadWall] ;
  TD.TapeHeadOffset = RDT.WallTransition.Final.TapeHead = TM.TapeHead - WallOffset ;

  if (VerificationEntry)
    {
    pVerif += WriteTransition (VerificationEntry + pVerif, RDT.WallTransition) ;

    // Save FinalSteps, FinalLeftmost, FinalRightmost in reserved space
    pFinal += Save32 (VerificationEntry + pFinal, TM.StepCount) ;
    pFinal += Save32 (VerificationEntry + pFinal, TM.Leftmost) ;
    pFinal += Save32 (VerificationEntry + pFinal, TM.Rightmost) ;

    pVerif += Save16 (VerificationEntry + pVerif, FinalAdjustment) ;
    pVerif += WriteTapeDescriptor (VerificationEntry + pVerif, &TM, TD) ;
    }

  // Augment each repeater run in InitialTape by one repeater
  for (uint32_t i = 0 ; i < nPartitions ; i++)
    InitialTape.Wall[i].insert (InitialTape.Wall[i].end(),
      InitialTape.Repeater[i].begin(), InitialTape.Repeater[i].end()) ;

  // Extend each end where appropriate
  int LeftmostShift = Cycle1Leftmost - Cycle2Leftmost ;
  if (LeftmostShift > 0)
    {
    InitialTape.Leftmost -= LeftmostShift ;
    ExpandTapeLeftward (TD, LeftmostShift) ;
    }

  int RightmostShift = Cycle2Rightmost - Cycle1Rightmost ;
  if (RightmostShift > 0)
    {
    InitialTape.Rightmost += RightmostShift ;
    ExpandTapeRightward (TD, RightmostShift) ;
    }

  if (!TestTapesEquivalent (InitialTape, TD)) return false ;
  CheckTape (&TM, InitialTape) ;

  if (nRuns > nRunsMax)
    {
    nRunsMax = nRuns ;
    nRunsMachine = SeedDatabaseIndex ;
    }

  // Write the info length
  if (VerificationEntry) Save32 (VerificationEntry + 8, pVerif - 12) ;

  switch (Type)
    {
    case BouncerType::Unilateral:  nUnilateral++ ; break ;
    case BouncerType::Bilateral:   nBilateral++ ; break ;
    case BouncerType::Translated:  nTranslated++ ; break ;
    default: break ;
    }

  if (Type == BouncerType::Translated)
    {
    // Don't count the dummy partition
    if (nPartitions > 2) nPartitioned++ ;
    }
  else if (nPartitions > 1) nPartitioned++ ;

  if (nRuns >= 6) nMultiple++ ;
  else if (nRuns >= 4) nDouble++ ;

  return true ;
  }

bool BouncerDecider::QuadraticProgression (int a1, int a2, int a3, int a4)
  {
  if (a3 - a2 <= a2 - a1) return false ; // Ignore arithmetic or descending progressions
  return a4 - 3 * a3 + 3 * a2 - a1 == 0 ;
  }

void BouncerDecider::GetRepetitionParams (int a1, int a2, int a3)
  {
  // Given a1, a2, a3 in quadratic progression, find the next two differences
  a3 -= a2 ;
  a2 -= a1 ;
  StepCount1 = 2 * a3 - a2 ;
  StepCount2 = 2 * StepCount1 - a3 ;
  }

bool BouncerDecider::FindRuns (Config* Cycle1, Config* Cycle2)
  {
  if (Cycle2[0] != Cycle1[0])
    printf ("\n%d: Internal error in FindRuns\n", SeedDatabaseIndex), exit (1) ;

  Config SavedSentinel[2] ;
  Config* OriginalCycle2 = 0 ;

  nRuns = 0 ;
  for ( ; ; )
    {
    if (nRuns == MAX_RUNS)
      printf("\n%d: MAX_RUNS exceeded\n", SeedDatabaseIndex), exit (1) ;
    RunData& R = RunDataArray[nRuns++] ;

    if (!FindRepeat (Cycle1, Cycle2, R)) return false ;

    if (nRuns == 1)
      {
      // Insert the end-of-cycle sentinels, after saving the originals
      OriginalCycle2 = R.Repeater ;
      SavedSentinel[0] = OriginalCycle2[0] ;
      SavedSentinel[1] = OriginalCycle2[StepCount2] ;
      OriginalCycle2[0].State = OriginalCycle2[StepCount2].State = 0 ;
      OriginalCycle2[0].Cell = 0 ; OriginalCycle2[StepCount2].Cell = 1 ; // Make them differnt
      }
    else
      {
      RunData& PrevR = RunDataArray[nRuns - 2] ;
      PrevR.Wall = R.Wall ;
      PrevR.WallSteps = R.WallSteps ;
      }

    if (R.Repeater == 0)
      {
      // Wrapped around
      nRuns-- ;
      break ;
      }

    Cycle1 += R.RepeaterSteps + R.WallSteps - R.RepeaterPeriod ;
    Cycle2 += R.RepeaterSteps + R.WallSteps ;
    if (Cycle2 -> State == 0)
      {
      // Wrapped around ending in an empty wall
      R.Wall = Cycle2 ;
      R.WallSteps = 0 ;
      break ;
      }
    }

  if (nRuns & 1) return false ;

  OriginalCycle2[0] = SavedSentinel[0] ;
  OriginalCycle2[StepCount2] = SavedSentinel[1] ;

  return true ;
  }

// bool BouncerDecider::FindRepeat (Config* Cycle1, Config* Cycle2, RunData& R)
//
// This function looks for Wall followed by Repeaters, and fills in R
// accordingly. This is in contrast to the meaning of R in the rest of the
// program, which is Repeaters followed by Wall

bool BouncerDecider::FindRepeat (Config* Cycle1, Config* Cycle2, RunData& R)
  {
  memset (&R, 0, sizeof (R)) ;
  R.Wall = Cycle2 ;

  // Find the number of matching steps
  uint32_t MatchLen ;
  for (MatchLen = 0 ; ; MatchLen++)
    if (Cycle1[MatchLen] != Cycle2[MatchLen]) break ;
  if (Cycle2[MatchLen].State == 0) // Wrapped around
    {
    R.Wall = Cycle2 ;
    R.WallSteps = MatchLen ;
    return true ;
    }

  // Look for repeaters
  //
  // A repeater is _acceptable_ if it is repeated at least MinRepeaterCount times
  // (including the first), and it covers at least MatchLen/4 steps.
  // MinRepeaterCount depends on MachineStates:
////TEMP  uint32_t MinRepeaterCount = (MachineStates == 5) ? 5 : 15 ;
uint32_t MinRepeaterCount = 5 ;

  // These two values are the product of trial and error:
  uint32_t MaxRepeaterPeriod = MatchLen / 4 ;
  uint32_t MinRepeaterSteps = MatchLen / 4 ;

  for (uint32_t RepeaterPeriod = 1 ; RepeaterPeriod < MaxRepeaterPeriod ; RepeaterPeriod++)
    {
    uint32_t p ;
    for (p = RepeaterPeriod + 1 ; p <= MatchLen ; p++)
      if (Cycle2[MatchLen - p] != Cycle2[MatchLen - p + RepeaterPeriod]) break ;
    p-- ;
    if (p < MinRepeaterSteps) continue ;
    uint32_t RepeaterCount = p / RepeaterPeriod ;
    if (RepeaterCount < MinRepeaterCount) continue ;
    if (R.Repeater == 0 || RepeaterCount * R.RepeaterPeriod > R.RepeaterSteps)
      {
      R.WallSteps = MatchLen - p ;
      R.Repeater = Cycle2 + R.WallSteps ;
      R.RepeaterPeriod = RepeaterPeriod ;
      R.RepeaterSteps = p ;
      }
    }

  if (R.Repeater == 0) return false ;

  // Cycle2 should have a whole number of repeated segments before it matches Cycle1 again
  uint32_t Diff ;
  for (Diff = 0 ; ; Diff++)
    if (Cycle2[MatchLen + Diff] != Cycle2[MatchLen + Diff - R.RepeaterPeriod])
      break ;
  if (Diff == 0) return false ;
  R.RepeaterSteps += Diff ;
  if (Diff % R.RepeaterPeriod != 0)
    {
    if (R.Repeater[R.RepeaterSteps].State != 0) return false ;

    // Wraparound
    R.RepeaterSteps += R.RepeaterPeriod - Diff ;
    }
  else R.RepeaterPeriod = Diff ;

  R.Direction = (R.Repeater[R.RepeaterSteps].TapeHead > R.Repeater[0].TapeHead) ? 1 : -1 ;

  if (R.RepeaterPeriod > this -> MaxRepeaterPeriod)
    {
    this -> MaxRepeaterPeriod = R.RepeaterPeriod ;
    MaxRepeaterMachine = SeedDatabaseIndex ;
    }

  return true ;
  }

// bool BouncerDecider::AssignPartitions()
//
// Count the partitions, and assign each run to its partition
// (number from left to right)

bool BouncerDecider::AssignPartitions()
  {
  int Leftmost = 0 ;
  int Rightmost = 0 ;
  int Partition = 0 ;
  RunDataArray[0].Partition = 0 ;
  for (uint32_t i = 1 ; i < nRuns ; i++)
    {
    RunData& R = RunDataArray[i] ;
    if (R.Direction == RunDataArray[i - 1].Direction)
      Partition += R.Direction ;
    R.Partition = Partition ;
    if (Partition < Leftmost) Leftmost = Partition ;
    else if (Partition > Rightmost) Rightmost = Partition ;
    }
  if (Leftmost != 0 && Rightmost != 0) return false ;
  if (RunDataArray[nRuns - 1].Partition != 0) return false ;

  // Add on the dummy partitions it it's a Translated Bouncer
  switch (TB_Direction)
    {
    case 1: Leftmost-- ; break ;
    case -1: Rightmost++ ; break ;
    }

  nPartitions = Rightmost - Leftmost + 1 ;
  if (nPartitions > MAX_PARTITIONS) return false ;
  for (uint32_t i = 0 ; i <= nRuns ; i++)
    RunDataArray[i].Partition -= Leftmost ;

  return true ;
  }

void BouncerDecider::GetMaxWallExtents()
  {
  for (uint32_t i = 0 ; i < nPartitions ; i++)
    {
    PartitionData& PD = PartitionDataArray[i] ;
    PD.MaxLeftWallExtent = INT_MIN ;
    PD.MinRightWallExtent = INT_MAX ;
    PD.MinRepeaterExtent = INT_MAX ;
    PD.MaxRepeaterExtent = INT_MIN ;
    }

  // Handle Translated Cycler
  switch (TB_Direction)
    {
    case 1:
      PartitionDataArray[0].MaxLeftWallExtent = TB_Outermost + TB_Wall.size() - 1 ;
      break ;

    case -1:
      PartitionDataArray[nPartitions - 1].MinRightWallExtent = TB_Outermost - TB_Wall.size() ;
      break ;
    }

  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    RunData& RD = RunDataArray[i] ;
    int LeftmostWall = INT_MAX ;
    int RightmostWall = INT_MIN ;
    if (RD.WallSteps == 0)
      {
      LeftmostWall = RD.Wall[0].TapeHead ;
      RightmostWall = LeftmostWall - 1 ;
      }
    for (uint32_t j = 0 ; j < RD.WallSteps ; j++)
      {
      if (RD.Wall[j].TapeHead < LeftmostWall) LeftmostWall = RD.Wall[j].TapeHead ;
      if (RD.Wall[j].TapeHead > RightmostWall) RightmostWall = RD.Wall[j].TapeHead ;
      }
    PartitionData& PD = PartitionDataArray[RD.Partition] ;
    if (RD.Direction == -1)
      {
      if (RightmostWall > PD.MaxLeftWallExtent) PD.MaxLeftWallExtent = RightmostWall ;
      if (RD.Partition > 0)
        if (LeftmostWall < PartitionDataArray[RD.Partition - 1].MinRightWallExtent)
          PartitionDataArray[RD.Partition - 1].MinRightWallExtent = LeftmostWall ;
      }
    else
      {
      if (LeftmostWall < PD.MinRightWallExtent) PD.MinRightWallExtent = LeftmostWall ;
      if (RD.Partition < nPartitions - 1)
        if (RightmostWall > PartitionDataArray[RD.Partition + 1].MaxLeftWallExtent)
          PartitionDataArray[RD.Partition + 1].MaxLeftWallExtent = RightmostWall ;
      }

    int& LeftmostRepeater = PartitionDataArray[RD.Partition].MinRepeaterExtent ;
    int& RightmostRepeater = PartitionDataArray[RD.Partition].MaxRepeaterExtent ;
    for (uint32_t j = 0 ; j < RD.RepeaterSteps ; j++)
      {
      if (RD.Repeater[j].TapeHead < LeftmostRepeater) LeftmostRepeater = RD.Repeater[j].TapeHead ;
      if (RD.Repeater[j].TapeHead > RightmostRepeater) RightmostRepeater = RD.Repeater[j].TapeHead ;
      }
    }

  // Handle Translated Cycler
  switch (TB_Direction)
    {
    case 1:
      PartitionDataArray[0].MinRepeaterExtent = TB_Outermost + TB_Wall.size() ;
      PartitionDataArray[0].MaxRepeaterExtent = PartitionDataArray[1].MaxLeftWallExtent ;
      break ;

    case -1:
      PartitionDataArray[nPartitions - 1].MinRepeaterExtent =  PartitionDataArray[nPartitions - 2].MinRightWallExtent ;
      PartitionDataArray[nPartitions - 1].MaxRepeaterExtent = TB_Outermost - TB_Wall.size() ;
      break ;
    }
  }

bool BouncerDecider::EqualiseRepeaters()
  {
  // Find the LCM of the repeater shifts for each partition
  for (uint32_t Partition = 0 ; Partition < nPartitions ; Partition++)
    PartitionDataArray[Partition].RepeaterShift = 1 ;

  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    RunData& RD = RunDataArray[i] ;
    RD.RepeaterShift = RD.Repeater[RD.RepeaterPeriod].TapeHead - RD.Repeater[0].TapeHead ;
    PartitionData& PD = PartitionDataArray[RD.Partition] ;
    PD.RepeaterShift = LCM (PD.RepeaterShift, std::abs (RD.RepeaterShift)) ;
    }

  // For each partition, give all its runs the same RepeaterShift and adjust
  // RepeaterPeriod accordingly
  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    RunData& RD = RunDataArray[i] ;
    PartitionData& PD = PartitionDataArray[RD.Partition] ;
    RD.RepeaterPeriod *= PD.RepeaterShift / std::abs (RD.RepeaterShift) ;
    RD.RepeaterShift = RD.Direction * PD.RepeaterShift ;
    }

  // Partition RepeaterCount is minimum of RepeaterCounts for each run
  for (uint32_t Partition = 0 ; Partition < nPartitions ; Partition++)
    PartitionDataArray[Partition].RepeaterCount = INT_MAX ;

  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    RunData& RD = RunDataArray[i] ;
    PartitionData& PD = PartitionDataArray[RD.Partition] ;
    uint32_t RepeaterCount = RD.RepeaterSteps / RD.RepeaterPeriod ;
    if (RepeaterCount < 3) return false ;
    if (RepeaterCount < PD.RepeaterCount) PD.RepeaterCount = RepeaterCount ;
    }

  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    RunData& RD = RunDataArray[i] ;
    RD.RepeaterCount = PartitionDataArray[RD.Partition].RepeaterCount ;
    int Remainder = RD.RepeaterSteps - RD.RepeaterCount * RD.RepeaterPeriod ;
    if (Remainder < 0) TM_ERROR() ;
    RD.Wall -= Remainder ;
    RD.WallSteps += Remainder ;
    RD.RepeaterSteps -= Remainder ;
    }

  return true ;
  }

bool BouncerDecider::MakeRunDescriptors()
  {
  // Convert all the RunData to RunDescriptors
  for (uint32_t i = 0 ; i < nRuns ; i++)
    ConvertRunData (RunDescriptorArray[i], RunDataArray[i]) ;
  return true ;
  }

void BouncerDecider::ConvertRunData (RunDataTransitions& To, const RunData& From)
  {
  To.Partition = From.Partition ;
  To.Direction = From.Direction ;

  To.RepeaterTransition.Initial.Tape.clear() ;
  To.RepeaterTransition.Final.Tape.clear() ;
  To.WallTransition.Initial.Tape.clear() ;
  To.WallTransition.Final.Tape.clear() ;

  // Construct To.RepeaterTransition
  int Leftmost, Rightmost ;
  Leftmost = Rightmost = From.Repeater[0].TapeHead ;
  for (uint32_t i = 1 ; i < From.RepeaterPeriod ; i++)
    {
    if (From.Repeater[i].TapeHead < Leftmost) Leftmost = From.Repeater[i].TapeHead ;
    if (From.Repeater[i].TapeHead > Rightmost) Rightmost = From.Repeater[i].TapeHead ;
    }

  To.RepeaterTransition.nSteps = From.RepeaterPeriod ;
  To.RepeaterTransition.Initial.State = From.Repeater[0].State ;
  To.RepeaterTransition.Initial.TapeHead = From.Repeater[0].TapeHead - Leftmost ;

  To.RepeaterTransition.Initial.Tape.resize (Rightmost - Leftmost + 1, TAPE_ANY) ;
  To.RepeaterTransition.Final.Tape.resize (Rightmost - Leftmost + 1, TAPE_ANY) ;

  for (uint32_t i = 0 ; i < From.RepeaterPeriod ; i++)
    {
    const Config& C = From.Repeater[i] ;
    int TapeHead = C.TapeHead - Leftmost ;
    if (To.RepeaterTransition.Initial.Tape[TapeHead] == TAPE_ANY)
      To.RepeaterTransition.Initial.Tape[TapeHead] = C.Cell ;
    To.RepeaterTransition.Final.Tape[TapeHead] = TM[C.State][C.Cell].Write ;
    }
  To.RepeaterTransition.Final.State = From.Repeater[From.RepeaterPeriod].State ;
  To.RepeaterTransition.Final.TapeHead = From.Repeater[From.RepeaterPeriod].TapeHead - Leftmost ;

  // Construct To.WallTransition
  To.WallTransition.nSteps = From.WallSteps ;
  if (From.WallSteps == 0)
    {
    To.WallTransition.Initial.State = To.WallTransition.Final.State =
      From.Wall[0].State ;
    To.WallTransition.Initial.TapeHead = To.WallTransition.Final.TapeHead = 0 ;
    return ;
    }

  Leftmost = Rightmost = From.Wall[0].TapeHead ;
  for (uint32_t i = 1 ; i < From.WallSteps ; i++)
    {
    if (From.Wall[i].TapeHead < Leftmost) Leftmost = From.Wall[i].TapeHead ;
    if (From.Wall[i].TapeHead > Rightmost) Rightmost = From.Wall[i].TapeHead ;
    }

  To.WallTransition.Initial.State = From.Wall[0].State ;
  To.WallTransition.Initial.TapeHead = From.Wall[0].TapeHead - Leftmost ;

  To.WallTransition.Initial.Tape.resize (Rightmost - Leftmost + 1, TAPE_ANY) ;
  To.WallTransition.Final.Tape.resize (Rightmost - Leftmost + 1, TAPE_ANY) ;

  for (uint32_t i = 0 ; i < From.WallSteps ; i++)
    {
    const Config& C = From.Wall[i] ;
    int TapeHead = C.TapeHead - Leftmost ;
    if (To.WallTransition.Initial.Tape[TapeHead] == TAPE_ANY)
      To.WallTransition.Initial.Tape[TapeHead] = C.Cell ;
    To.WallTransition.Final.Tape[TapeHead] = TM[C.State][C.Cell].Write ;
    }

  To.WallTransition.Final.State = From.Wall[From.WallSteps].State ;
  To.WallTransition.Final.TapeHead = From.Wall[From.WallSteps].TapeHead - Leftmost ;
  }

void BouncerDecider::PrintTape (const TapeDescriptor& Tape)
  {
  printf ("%c %d.%d ", Tape.State + '@', Tape.TapeHeadWall, Tape.TapeHeadOffset) ;
  for (uint32_t i = 0 ; i < nPartitions ; i++)
    {
    for (uint32_t j = 0 ; j < Tape.Wall[i].size() ; j++) printf ("%d", Tape.Wall[i][j]) ;
    printf (" %d*", Tape.RepeaterCount[i]) ;
    for (uint32_t j = 0 ; j < Tape.Repeater[i].size() ; j++) printf ("%d", Tape.Repeater[i][j]) ;
    printf (" ") ;
    }
  for (uint32_t j = 0 ; j < Tape.Wall[nPartitions].size() ; j++) printf ("%d", Tape.Wall[nPartitions][j]) ;
  printf ("\n") ;
  }

bool BouncerDecider::CheckRuns()
  {
  // Check that the run lengths are consistent
  for (uint32_t i = 0 ; ; i++)
    {
    RunData& RD = RunDataArray[i] ;
    if (RD.Wall != RD.Repeater + RD.RepeaterSteps) TM_ERROR() ;

    if (i == nRuns - 1) break ;

    if (RunDataArray[i + 1].Repeater != RD.Wall + RD.WallSteps) TM_ERROR() ;
    }

  // Check that the repeaters repeat
  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    RunData& RD = RunDataArray[i] ;
    for (uint32_t j = RD.RepeaterPeriod ; j < RD.RepeaterSteps ; j++)
      if (RD.Repeater[j] != RD.Repeater[j - RD.RepeaterPeriod])
        TM_ERROR() ;
    }

  // Check that all RepeaterCount and RepeaterShift fields for each Partition are equal
  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    const RunData& RD = RunDataArray[i] ;
    PartitionData& PD = PartitionDataArray[RD.Partition] ;
    if (RD.RepeaterCount != PD.RepeaterCount)
      {
      if (TraceOutput)
        printf ("\n%d: Error 1 in CheckRuns\n", SeedDatabaseIndex) ;
      return false ;
      }
    if (std::abs (RD.RepeaterShift) != PD.RepeaterShift)
      {
      if (TraceOutput)
        printf ("\n%d: Error 5 in CheckRuns\n", SeedDatabaseIndex) ;
      return false ;
      }
    }

  // Check the RunData array
  Config* C = RunDataArray[0].Repeater ;
  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    const RunData& RD = RunDataArray[i] ;

    if (C != RD.Repeater)
      {
      if (TraceOutput)
        printf ("\n%d.%d: Error 4 in CheckRuns\n", SeedDatabaseIndex, i) ;
      return false ;
      }
    C += RD.RepeaterPeriod ;
    for (uint32_t j = 1 ; j < RD.RepeaterCount ; j++)
      for (uint32_t k = 0 ; k < RD.RepeaterPeriod ; k++)
        {
        if (*C != RD.Repeater[k])
          {
          if (TraceOutput)
            printf ("\n%d.%d.%d.%d: Error 3 in CheckRuns\n", SeedDatabaseIndex, i, j, k) ;
          return false ;
          }
        C++ ;
        }
    if (C != RD.Wall)
      {
      if (TraceOutput)
        printf ("\n%d.%d: Error 2 in CheckRuns\n", SeedDatabaseIndex, i) ;
      return false ;
      }
    C += RD.WallSteps ;
    }

  // Check the RunDataTransitions array
  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    RunDataTransitions& SD = RunDescriptorArray[i] ;
    RunDataTransitions& NextSD = RunDescriptorArray[(i + 1) % nRuns] ;

    CheckTransition (SD.RepeaterTransition) ;
    CheckTransition (SD.WallTransition) ;

    CheckFollowOn (SD.RepeaterTransition.Final, SD.RepeaterTransition.Initial) ;
    if (SD.WallTransition.Initial.Tape.empty())
      CheckFollowOn (SD.RepeaterTransition.Final, NextSD.RepeaterTransition.Initial) ;
    else
      {
      CheckFollowOn (SD.RepeaterTransition.Final, SD.WallTransition.Initial) ;
      CheckFollowOn (SD.WallTransition.Final, NextSD.RepeaterTransition.Initial) ;
      }
    }

  return true ;
  }

static uint32_t LCM (uint32_t x, uint32_t y)
  {
  uint32_t d = GCD (x, y) ;

  // Now we want x * y / d, avoiding overflow if possible
  x /= d ;
  y /= d ;
  d *= x * y ;
  if (d % x || d % y)
    printf ("\nOverflow in LCM\n"), exit (1) ;
  return d ;
  }

static uint32_t GCD (uint32_t x, uint32_t y)
  {
  for ( ; ; )
    {
    x %= y ; if (x == 0) return y ;
    y %= x ; if (y == 0) return x ;
    }
  }

void BouncerDecider::DumpRunData()
  {
  printf ("StepCount2 = %d\n", StepCount2) ;
  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    RunData& RD = RunDataArray[i] ;
    printf ("Repeater * %d:", RD.RepeaterCount) ;
    for (uint32_t j = 0 ; j < RD.RepeaterPeriod ; j++)
      printf (" %d %c%d", RD.Repeater[j].TapeHead, '@' + RD.Repeater[j].State, RD.Repeater[j].Cell) ;
    printf ("\n") ;
    printf ("Wall:") ;
    if ((int)RD.WallSteps < 0)
      printf ("\n%d: WallSteps = %d!\n", SeedDatabaseIndex, RD.WallSteps), exit (1) ;
    for (uint32_t j = 0 ; j < RD.WallSteps ; j++)
      printf (" %d %c%d", RD.Wall[j].TapeHead, '@' + RD.Wall[j].State, RD.Wall[j].Cell) ;
    printf ("\n") ;
    }
  }

void BouncerDecider::DumpTransitions()
  {
  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    PrintTransition (RunDescriptorArray[i].RepeaterTransition) ;
    PrintTransition (RunDescriptorArray[i].WallTransition) ;
    printf ("\n") ;
    }
  printf ("\n") ;
  }

void BouncerDecider::PrintTransition (const SegmentTransition& Tr)
  {
  int i ;
  printf ("  %c: ", Tr.Initial.State + '@') ;
  if (Tr.Initial.Tape.empty())
    {
    printf ("[ ]\n") ;
    return ;
    }
  if (Tr.Initial.TapeHead == 0) printf ("[") ;
  for (i = 0 ; i < (int)Tr.Initial.Tape.size() ; i++)
    {
    printf ("%d", Tr.Initial.Tape[i]) ;
    printf (i == Tr.Initial.TapeHead - 1 ? "[" : i == Tr.Initial.TapeHead ? "]" : " ") ;
    }

  printf (" -> %c: ", Tr.Final.State + '@') ;
  if (Tr.Final.TapeHead == -1) printf ("[ ]") ;
  else if (Tr.Final.TapeHead == 0) printf ("[") ;
  for (i = 0 ; i < (int)Tr.Final.Tape.size() ; i++)
    {
    printf ("%d", Tr.Final.Tape[i]) ;
    printf (i == Tr.Final.TapeHead - 1 ? "[" : i == Tr.Final.TapeHead ? "]" : " ") ;
    }
  if (i == Tr.Final.TapeHead) printf (" ]") ;
  printf ("\n") ;
  }

// bool BouncerDecider::AnalyseTape (const TuringMachine* TM, TapeDescriptor& TD,
//   uint32_t CurrentWall, uint32_t CurrentPartition, const SegmentTransition& Tr, int Leftmost, int Rightmost)
//
// Called before executing a Repeater SegmentTransition

BouncerDecider::ORA_Result BouncerDecider::AnalyseTape (const TuringMachine* TM, TapeDescriptor& TD,
  uint32_t CurrentWall, uint32_t CurrentPartition, const SegmentTransition& Tr, int Leftmost, int Rightmost)
  {
  TD.State = TM -> State ;

  int WallLeftmost[MAX_PARTITIONS + 1] ;
  int WallRightmost[MAX_PARTITIONS + 1] ;

  for (uint32_t Partition = 0 ; Partition < nPartitions ; Partition++)
    {
    const PartitionData& PD = PartitionDataArray[Partition] ;
    TD.Repeater[Partition].resize (PD.RepeaterShift) ;
    TD.RepeaterCount[Partition] = PD.RepeaterCount ;
    }

  int MinRepeaterLen ;
  int SequenceStart, SequenceEnd ;

  // Assign wall boundaries from the left rightwards to the current partition
  WallLeftmost[0] = TD.Leftmost ;
  for (uint32_t Wall = 0 ; Wall < CurrentPartition ; Wall++)
    {
    const PartitionData& PD = PartitionDataArray[Wall] ;
    if (!GetRepeaterExtent_rightward (TM, Wall, SequenceStart, SequenceEnd, Leftmost, Rightmost))
      return DecrementRepeaterCount (Wall) ;

    WallRightmost[Wall] = SequenceStart - 1 ;

    MinRepeaterLen = PD.RepeaterCount * PD.RepeaterShift ;
    if (SequenceEnd >= SequenceStart + MinRepeaterLen)
      SequenceEnd = SequenceStart + MinRepeaterLen - 1 ;
    Leftmost = WallLeftmost[Wall + 1] = SequenceEnd + 1 ;
    }

  // Assign wall boundaries from the right leftwards to the current partition
  WallRightmost[nPartitions] = TD.Rightmost ;
  for (uint32_t Wall = nPartitions ; Wall > CurrentPartition + 1 ; Wall--)
    {
    const PartitionData& PD = PartitionDataArray[Wall - 1] ;
    if (!GetRepeaterExtent_leftward (TM, Wall - 1, SequenceStart, SequenceEnd, Leftmost, Rightmost))
      return DecrementRepeaterCount (Wall - 1) ;

    WallLeftmost[Wall] = SequenceEnd + 1 ;

    MinRepeaterLen = PD.RepeaterCount * PD.RepeaterShift ;
    if (SequenceStart <= SequenceEnd - MinRepeaterLen)
      SequenceStart = SequenceEnd - MinRepeaterLen + 1 ;
    Rightmost = WallRightmost[Wall - 1] = SequenceStart - 1 ;
    }

  // Assign wall boundaries for the current partition
  const PartitionData& PD = PartitionDataArray[CurrentPartition] ;
  MinRepeaterLen = PD.RepeaterCount * PD.RepeaterShift ;
  if (Tr.Final.TapeHead < Tr.Initial.TapeHead)
    {
    // Leftward run
    if (Rightmost > TM -> TapeHead + MinRepeaterLen / 2)
      Rightmost = TM -> TapeHead + MinRepeaterLen / 2 ;
    if (!GetRepeaterExtent_leftward (TM, CurrentPartition, SequenceStart, SequenceEnd, Leftmost, Rightmost))
      return DecrementRepeaterCount (CurrentPartition) ;

    // Adjust SequenceEnd so that Tr.Initial.Tape starts with exactly one repeater
    if (SequenceEnd < TM -> TapeHead - Tr.Initial.TapeHead + PD.RepeaterShift - 1)
      {
      if (CurrentPartition == nPartitions - 1) TM_ERROR() ;
      return DecrementRepeaterCount (CurrentPartition + 1) ;
      }
    SequenceEnd = TM -> TapeHead - Tr.Initial.TapeHead + PD.RepeaterShift - 1 ;
    if (SequenceEnd > WallRightmost[CurrentPartition + 1]) TM_ERROR() ;

    if (SequenceEnd < SequenceStart + MinRepeaterLen - 1)
      return DecrementRepeaterCount (CurrentPartition) ;

    if (CurrentPartition != nPartitions - 1 &&
      (int)Tr.Initial.Tape.size() - Tr.Initial.TapeHead >
        WallRightmost[CurrentPartition + 1] - TM -> TapeHead)
          return DecrementRepeaterCount (CurrentPartition + 1) ;

    SequenceStart = SequenceEnd - MinRepeaterLen + 1 ;
    }
  else
    {
    // Rightward run
    if (Leftmost < TM -> TapeHead - MinRepeaterLen / 2)
      Leftmost = TM -> TapeHead - MinRepeaterLen / 2 ;
    if (!GetRepeaterExtent_rightward (TM, CurrentPartition, SequenceStart, SequenceEnd, Leftmost, Rightmost))
      return DecrementRepeaterCount (CurrentPartition) ;

    // Adjust SequenceStart so that Tr.Initial.Tape ends with exactly one repeater
    if (SequenceStart > int(Tr.Initial.Tape.size() - Tr.Initial.TapeHead + TM -> TapeHead - PD.RepeaterShift))
      {
      if (CurrentPartition == 0) TM_ERROR() ;
      return DecrementRepeaterCount (CurrentPartition - 1) ;
      }
    SequenceStart = Tr.Initial.Tape.size() - Tr.Initial.TapeHead + TM -> TapeHead - PD.RepeaterShift ;
    if (SequenceStart < WallLeftmost[CurrentPartition]) TM_ERROR() ;

    if (SequenceEnd < SequenceStart + MinRepeaterLen - 1)
      return DecrementRepeaterCount (CurrentPartition) ;

    if (CurrentPartition && Tr.Initial.TapeHead > TM -> TapeHead - WallLeftmost[CurrentPartition])
      return DecrementRepeaterCount (CurrentPartition - 1) ;

    SequenceEnd = SequenceStart + MinRepeaterLen - 1 ;
    }

  WallLeftmost[CurrentPartition + 1] = SequenceEnd + 1 ;
  WallRightmost[CurrentPartition] = SequenceStart - 1 ;

  for (uint32_t Partition = 0 ; ; Partition++)
    {
    if (WallLeftmost[Partition] > WallRightmost[Partition] + 1)
      return DecrementRepeaterCount (Partition) ;

    int WallLen = WallRightmost[Partition] - WallLeftmost[Partition] + 1 ;
    TD.Wall[Partition].resize (WallLen) ;
    memcpy (&TD.Wall[Partition][0], &TM -> Tape[WallLeftmost[Partition]], WallLen) ;

    if (Partition == nPartitions) break ;

    const PartitionData& PD = PartitionDataArray[Partition] ;
    memcpy (&TD.Repeater[Partition][0], &TM -> Tape[WallLeftmost[Partition] + WallLen], PD.RepeaterShift) ;
    }

  TD.TapeHeadWall = CurrentWall ;
  TD.TapeHeadOffset = TM -> TapeHead - WallLeftmost[CurrentWall] ;

  return ORA_Result::OK ;
  }

bool BouncerDecider::GetRepeaterExtent_leftward (const TuringMachine* TM, uint32_t Partition,
  int& SequenceStart, int& SequenceEnd, int LeftLimit, int RightLimit)
  {
  PartitionData& PD = PartitionDataArray[Partition] ;
  int MinSequenceLen = PD.RepeaterCount * PD.RepeaterShift ;

  if (Partition != 0)
    {
    if (LeftLimit < PartitionDataArray[Partition - 1].MinRightWallExtent - PD.RepeaterShift)
      LeftLimit = PartitionDataArray[Partition - 1].MinRightWallExtent - PD.RepeaterShift ;
    }
  if (Partition != nPartitions - 1)
    {
    if (RightLimit - MinSequenceLen > PD.MaxLeftWallExtent + PD.RepeaterShift)
      RightLimit = PD.MaxLeftWallExtent + PD.RepeaterShift + MinSequenceLen ;
    if (RightLimit > PartitionDataArray[Partition + 1].MaxLeftWallExtent + PD.RepeaterShift)
      RightLimit = PartitionDataArray[Partition + 1].MaxLeftWallExtent + PD.RepeaterShift ;
    }

  SequenceEnd = RightLimit ;
  SequenceStart = SequenceEnd - PD.RepeaterShift ;
  for ( ; ; )
    {
    while (TM -> Tape[SequenceStart] != TM -> Tape[SequenceStart + PD.RepeaterShift])
      SequenceStart-- ;
    SequenceEnd = SequenceStart + PD.RepeaterShift ;
    if (SequenceEnd - MinSequenceLen < LeftLimit - 1) return false ;
    while (SequenceStart >= LeftLimit &&
      TM -> Tape[SequenceStart] == TM -> Tape[SequenceStart + PD.RepeaterShift])
        SequenceStart-- ;
    if (SequenceStart <= SequenceEnd - MinSequenceLen &&
      SequenceStart < int(PD.MaxRepeaterExtent - PD.RepeaterShift) &&
        SequenceEnd >= int(PD.MinRepeaterExtent + PD.RepeaterShift))
          {
          SequenceStart++ ;
          return true ;
          }
    SequenceEnd = SequenceStart + PD.RepeaterShift ;
    if (SequenceEnd - MinSequenceLen < LeftLimit) return false ;
    }
  }

bool BouncerDecider::GetRepeaterExtent_rightward (const TuringMachine* TM, uint32_t Partition,
  int& SequenceStart, int& SequenceEnd, int LeftLimit, int RightLimit)
  {
  PartitionData& PD = PartitionDataArray[Partition] ;
  int MinSequenceLen = PD.RepeaterCount * PD.RepeaterShift ;

  if (Partition != 0)
    {
    if (LeftLimit + MinSequenceLen < PD.MinRightWallExtent - PD.RepeaterShift)
      LeftLimit = PD.MinRightWallExtent - PD.RepeaterShift - MinSequenceLen ;
    if (LeftLimit < PartitionDataArray[Partition - 1].MinRightWallExtent - PD.RepeaterShift)
      LeftLimit = PartitionDataArray[Partition - 1].MinRightWallExtent - PD.RepeaterShift ;
    }
  if (Partition != nPartitions - 1)
    {
    if (RightLimit > PartitionDataArray[Partition + 1].MaxLeftWallExtent + PD.RepeaterShift)
      RightLimit = PartitionDataArray[Partition + 1].MaxLeftWallExtent + PD.RepeaterShift ;
    }

  SequenceStart = LeftLimit ;
  SequenceEnd = SequenceStart + PD.RepeaterShift ;
  for ( ; ; )
    {
    while (TM -> Tape[SequenceEnd] != TM -> Tape[SequenceEnd - PD.RepeaterShift])
      SequenceEnd++ ;
    SequenceStart = SequenceEnd - PD.RepeaterShift ;
    if (SequenceStart + MinSequenceLen > RightLimit + 1) return false ;
    while (SequenceEnd <= RightLimit &&
      TM -> Tape[SequenceEnd] == TM -> Tape[SequenceEnd - PD.RepeaterShift])
        SequenceEnd++ ;
    if (SequenceEnd >= SequenceStart + MinSequenceLen &&
      SequenceStart <= int(PartitionDataArray[Partition].MaxRepeaterExtent - PD.RepeaterShift) &&
        SequenceEnd > int(PartitionDataArray[Partition].MinRepeaterExtent + PD.RepeaterShift))
          {
          SequenceEnd-- ;
          return true ;
          }
    SequenceStart = SequenceEnd - PD.RepeaterShift ;
    if (SequenceStart + MinSequenceLen > RightLimit) return false ;
    }
  }

// ORA_Result BouncerDecider::DecrementRepeaterCount (uint32_t Partition)
//
// Decrement the repeater count for Partition and adjust the destination Wall
//
// Returns: ORA_Result::ABORT if the repeater count would fall below 5
//          ORA_Result::RETRY otherwise

BouncerDecider::ORA_Result BouncerDecider::DecrementRepeaterCount (uint32_t Partition)
  {
  // Decrement the repeater count for this partition and request a retry
  PartitionData& PD = PartitionDataArray[Partition] ;
  if (PD.RepeaterCount < 5) return ORA_Result::ABORT ;
  PD.RepeaterCount-- ;

  // Adjust all runs in this partition
  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    RunData& RD = RunDataArray[i] ;
    if (RD.Partition == Partition)
      {
      RD.RepeaterCount-- ;
      RD.RepeaterSteps -= RD.RepeaterPeriod ;
      RD.Wall -= RD.RepeaterPeriod ;
      RD.WallSteps += RD.RepeaterPeriod ;
      }
    }
  return ORA_Result::RETRY ;
  }

uint32_t BouncerDecider::WriteTapeDescriptor (uint8_t* VerificationEntry,
  const TuringMachine* TM, const TapeDescriptor& TD) const
  {
  uint32_t pVerif = 0 ;
  VerificationEntry[pVerif++] = TM -> State ;
  VerificationEntry[pVerif++] = TD.TapeHeadWall ;
  pVerif += Save16 (VerificationEntry + pVerif, TD.TapeHeadOffset) ;
  for (uint32_t i = 0 ; i <= nPartitions ; i++)
    {
    pVerif += Save16 (VerificationEntry + pVerif, TD.Wall[i].size()) ;
    memcpy (VerificationEntry + pVerif, &TD.Wall[i][0], TD.Wall[i].size()) ;
    pVerif += TD.Wall[i].size() ;
    }
  for (uint32_t i = 0 ; i < nPartitions ; i++)
    {
    pVerif += Save16 (VerificationEntry + pVerif, TD.Repeater[i].size()) ;
    memcpy (VerificationEntry + pVerif, &TD.Repeater[i][0], TD.Repeater[i].size()) ;
    pVerif += TD.Repeater[i].size() ;
    }
  return pVerif ;
  }

uint32_t BouncerDecider::WriteTransition (uint8_t* VerificationEntry, const SegmentTransition& Tr) const
  {
  uint32_t pVerif = Save16 (VerificationEntry, Tr.nSteps) ;
  pVerif += WriteSegment (VerificationEntry + pVerif, Tr.Initial) ;
  pVerif += WriteSegment (VerificationEntry + pVerif, Tr.Final) ;
  return pVerif ;
  }

uint32_t BouncerDecider::WriteSegment (uint8_t* VerificationEntry, const Segment& Seg) const
  {
  uint32_t pVerif = 0 ;
  VerificationEntry[pVerif++] = Seg.State ;
  pVerif += Save16 (VerificationEntry + pVerif, Seg.TapeHead) ;
  pVerif += Save16 (VerificationEntry + pVerif, Seg.Tape.size()) ;
  memcpy (VerificationEntry + pVerif, &Seg.Tape[0], Seg.Tape.size()) ;
  return pVerif + Seg.Tape.size() ;
  }

bool BouncerDecider::MakeTranslatedBouncerData()
  {
  if (TB_Direction == 1) // Translation to the right
    {
    // We expect a repeater sequence starting from Cycle1Leftmost and moving left
    TB_Size = Cycle1Leftmost - Leftmost ;
    TB_Outermost = Leftmost ;
    int Shift = Cycle2Leftmost - Cycle1Leftmost ;
    TB_Repeater = ustring (Tape + Cycle1Leftmost - Shift, Shift) ;
    int RepeaterLen ;
    for (RepeaterLen = Shift + 1 ; (int)RepeaterLen <= Cycle1Leftmost - Leftmost ; RepeaterLen++)
      if (Tape[Cycle1Leftmost - RepeaterLen] != Tape[Cycle1Leftmost - RepeaterLen + Shift])
        break ;
    RepeaterLen-- ;
    PartitionData& PD = PartitionDataArray[0] ;
    PD.RepeaterShift = Shift ;
    TB_RepeaterCount = PD.RepeaterCount = RepeaterLen / Shift ;
    if (PD.RepeaterCount < 3) return false ;
    RepeaterLen = PD.RepeaterCount * Shift ;

    TB_Wall = ustring (Tape + Leftmost, TB_Wall.size()) ;
    }
  else if (TB_Direction == -1) // Translation to the left
    {
    // We expect a repeater sequence starting from Cycle1Rightmost and moving right
    TB_Size = Rightmost - Cycle1Rightmost ;
    TB_Outermost = Rightmost ;
    int Shift = Cycle1Rightmost - Cycle2Rightmost ;
    TB_Repeater = ustring (Tape + Cycle1Rightmost + 1, Shift) ;
    int RepeaterLen ;
    for (RepeaterLen = Shift + 1 ; (int)RepeaterLen <= Rightmost - Cycle1Rightmost ; RepeaterLen++)
      if (Tape[Cycle1Rightmost + RepeaterLen] != Tape[Cycle1Rightmost + RepeaterLen - Shift])
        break ;
    RepeaterLen-- ;
    PartitionData& PD = PartitionDataArray[nPartitions - 1] ;
    PD.RepeaterShift = Shift ;
    TB_RepeaterCount = PD.RepeaterCount = RepeaterLen / Shift ;
    if (PD.RepeaterCount < 3) return false ;
    RepeaterLen = PD.RepeaterCount * Shift ;

    TB_Wall = ustring (Tape + Cycle1Rightmost, TB_Wall.size()) ;
    }
  return true ;
  }

// bool BouncerDecider::RemoveGap (TapeDescriptor TD, const SegmentTransition& Tr)
//
// If there is a gap between the wall and Tr.Initial.Tape, close it by adding
// Repeaters to the current wall, and removing them from the destination wall
//
// Returns true if any gap was removed

bool BouncerDecider::RemoveGap (TapeDescriptor& TD, const SegmentTransition& Tr)
  {
  uint32_t Wall = TD.TapeHeadWall ;
  if (Tr.Final.TapeHead < Tr.Initial.TapeHead)
    {
    // Leftward repeater
    uint32_t Stride = Tr.Initial.TapeHead - Tr.Final.TapeHead ;

    // How much does Tr.Initial.Tape overhang the Wall into the array of Repeaters?
    int Overhang = Tr.Initial.TapeHead - TD.TapeHeadOffset ;
    if (Overhang <= 0) return false ;

    // If there is a gap between the wall and Tr.Initial.Tape (which can only
    // happen if TD.Wall[Wall].TapeHeadOffset <= -2), close it by prepending
    // Repeaters to the current wall, and removing them from the destination wall
    int Gap = Tr.Initial.TapeHead - Tr.Initial.Tape.size() - TD.TapeHeadOffset ;
    if (Gap <= 0) return false ;

    // Round up to a multiple of Stride
    Gap += Stride - 1 ; Gap -= Gap % Stride ;

    // Check that the destination wall ends with copies of the Repeater
    int Rotate = Gap % Stride ; Rotate = Stride - Rotate ; Rotate %= Stride ;
    if ((int)TD.Wall[Wall - 1].size() < Gap) TM_ERROR() ;
    for (int i = 0 ; i < Gap ; i++)
      if (TD.Wall[Wall - 1][i + TD.Wall[Wall - 1].size() - Gap] !=
        TD.Repeater[Wall - 1][(i + Rotate) % Stride])
          TM_ERROR() ;

    // Prepend Repeaters to the current wall
    TD.Wall[Wall].insert (TD.Wall[Wall].begin(), Gap, 0) ;
    for (int i = 0 ; i < Gap ; i++)
      TD.Wall[Wall][i] = TD.Repeater[Wall - 1][(i + Rotate) % Stride] ;

    // Remove them from the destination wall
    TD.Wall[Wall - 1].erase (TD.Wall[Wall - 1].end() - Gap, TD.Wall[Wall - 1].end()) ;

    // Rotate the Repeater accordingly
    ustring Repeater = TD.Repeater[Wall - 1] ;
    for (uint32_t i = 0 ; i < Repeater.size() ; i++)
      TD.Repeater[Wall - 1][i] = Repeater[(i + Rotate) % Stride] ;

    TD.TapeHeadOffset += Gap ;
    }
  else
    {
    // Rightward repeater
    int InitOffset = TD.TapeHeadOffset - Tr.Initial.TapeHead ;
    uint32_t Stride = Tr.Final.TapeHead - Tr.Initial.TapeHead ;

    // How much does Tr.Initial.Tape overhang the Wall into the array of Repeaters?
    int Overhang = InitOffset + Tr.Initial.Tape.size() - TD.Wall[Wall].size() ;
    if (Overhang <= 0) return false ;

    // If there is a gap between the wall and Tr.Initial.Tape (which can only
    // happen if TD.TapeHeadOffset > TD.Wall[Wall].size()), close it by appending
    // Repeaters to the current wall, and removing them from the destination wall
    int Gap = TD.TapeHeadOffset - TD.Wall[Wall].size() - Tr.Initial.TapeHead ;
    if (Gap <= 0) return false ;

    // Round up to a multiple of Stride
    Gap += Stride - 1 ; Gap -= Gap % Stride ;

    // Check that the destination wall starts with copies of the Repeater
    if ((int)TD.Wall[Wall + 1].size() < Gap)
      TM_ERROR() ;
    for (int i = 0 ; i < Gap ; i++)
      if (TD.Wall[Wall + 1][i] != TD.Repeater[Wall][i % Stride])
        TM_ERROR() ;

    // Append Repeaters to the current wall
    for (int i = 0 ; i < Gap ; i++)
      TD.Wall[Wall].push_back (TD.Repeater[Wall][i % Stride]) ;

    // Remove them from the destination wall
    TD.Wall[Wall + 1].erase (TD.Wall[Wall + 1].begin(), TD.Wall[Wall + 1].begin() + Gap) ;

    // Rotate the Repeater accordingly
    ustring Repeater = TD.Repeater[Wall] ;
    for (uint32_t i = 0 ; i < Repeater.size() ; i++)
      TD.Repeater[Wall][i] = Repeater[(i + Gap) % Stride] ;
    }
  return true ;
  }

// bool BouncerDecider::TruncateWall (TapeDescriptor& TD, const SegmentTransition& Tr)
//
// Ensure that Tr.Initial.Tape extends at least as far as TD.Wall,
// by truncating the wall if necessary

bool BouncerDecider::TruncateWall (TapeDescriptor& TD, const SegmentTransition& Tr)
  {
  uint32_t Wall = TD.TapeHeadWall ;
  if (Tr.Final.TapeHead < Tr.Initial.TapeHead)
    {
    // Leftward repeater
    uint32_t Stride = Tr.Initial.TapeHead - Tr.Final.TapeHead ;

    // How much does Tr.Initial.Tape overhang the Wall into the array of Repeaters?
    int Overhang = Tr.Initial.TapeHead - TD.TapeHeadOffset ;
    if (Overhang >= 0) return false ;
    Overhang = -Overhang ; // 'Underhang' now

    // Check that the remaining wall consists of appropriately aligned copies of the Repeater
    for (int i = 0 ; i < Overhang ; i++)
      if (TD.Wall[Wall][i] != TD.Repeater[Wall - 1][i % Stride])
        TM_ERROR() ;

    // Re-align the wall so that the repeaters start immediately to the left of Tr.Initial.Tape
    TD.Wall[Wall - 1].insert (TD.Wall[Wall - 1].end(),
      TD.Wall[Wall].begin(), TD.Wall[Wall].begin() + Overhang) ;
    TD.Wall[Wall].erase (TD.Wall[Wall].begin(), TD.Wall[Wall].begin() + Overhang) ;

    // Rotate the Repeater accordingly
    ustring Repeater = TD.Repeater[Wall - 1] ;
    for (uint32_t i = 0 ; i < Repeater.size() ; i++)
      TD.Repeater[Wall - 1][i] = Repeater[(i + Overhang) % Stride] ;

    // This rotated repeater should be an initial segment of Tr.Initial.Tape
    for (uint32_t i = 0 ; i < Repeater.size() ; i++)
      if (TD.Repeater[Wall - 1][i] != Tr.Initial.Tape[i])
        TM_ERROR() ;

    TD.TapeHeadOffset -= Overhang ;
    }
  else
    {
    // Rightward repeater
    int InitOffset = TD.TapeHeadOffset - Tr.Initial.TapeHead ;
    uint32_t Stride = Tr.Final.TapeHead - Tr.Initial.TapeHead ;

    // How much does Tr.Initial.Tape overhang the Wall into the array of Repeaters?
    int Overhang = InitOffset + Tr.Initial.Tape.size() - TD.Wall[Wall].size() ;
    if (Overhang >= 0) return false ;
    Overhang = -Overhang ; // 'Underhang' now

    // Check that the remaining wall consists of appropriately aligned copies of the Repeater
    for (uint32_t i = InitOffset + Tr.Initial.Tape.size() ; i < TD.Wall[Wall].size() ; i++)
      {
      int t = TD.Wall[Wall].size() - i ;
      t %= Stride ; t = Stride - t ; t %= Stride ;
      if (TD.Wall[Wall][i] != TD.Repeater[Wall][t])
        TM_ERROR() ;
      }

    // Re-align the walls so that the repeaters start immediately after Tr.Initial.Tape
    TD.Wall[Wall + 1].insert (TD.Wall[Wall + 1].begin(),
      TD.Wall[Wall].begin() + InitOffset + Tr.Initial.Tape.size(),
        TD.Wall[Wall].end()) ;
    TD.Wall[Wall].resize (InitOffset + Tr.Initial.Tape.size()) ;

    // Rotate the Repeater accordingly
    ustring Repeater = TD.Repeater[Wall] ;
    for (uint32_t i = 0 ; i < Repeater.size() ; i++)
      TD.Repeater[Wall][(i + Overhang) % Stride] = Repeater[i] ;

    // This rotated repeater should be a final segment of Tr.Initial.Tape
    for (uint32_t i = 0 ; i < Repeater.size() ; i++)
      if (TD.Repeater[Wall][i] != Tr.Initial.Tape[Tr.Initial.Tape.size() - Stride + i])
        TM_ERROR() ;
    }
  return true ;
  }

BouncerDecider::ORA_Result BouncerDecider::ConstructTransitions (TapeDescriptor& InitialTape,
  uint32_t i, const TapeDescriptor& TD, RunDataTransitions& RDT)
  {
  const RunData& RD = RunDataArray[i % nRuns] ;
  uint32_t RepeaterSize = std::abs (RD.RepeaterShift) ;
  uint8_t State = TD.State ;

  SegmentTransition& WallTr = RDT.WallTransition ;
  SegmentTransition& RepTr = RDT.RepeaterTransition ;

  RDT.Partition = TD.TapeHeadWall ;
  RDT.Direction = RD.Direction ;
  WallTr.Initial.State = State ;
  WallTr.nSteps = 0 ;
  WallTr.Initial.TapeHead = TD.TapeHeadOffset ;
  uint32_t WallSize = TD.Wall[TD.TapeHeadWall].size() ;
  uint32_t WindowSize = WallSize + 2 * RepeaterSize ;
  ustring Window (WindowSize, 0) ;

  WallTr.Initial.Tape = TD.Wall[TD.TapeHeadWall] ;

  if (RD.Direction == 1) // Left-to-right
    {
    if (i && WallSize == 0) return MoveRepeaterIntoWall (InitialTape, TD, true) ;
    int TapeHead = TD.TapeHeadOffset ;
    if (TapeHead < 0) return MoveRepeaterIntoWall (InitialTape, TD, true) ;

    memcpy (&Window[0], &TD.Wall[TD.TapeHeadWall][0], WallSize) ;
    memcpy (&Window[WallSize], &TD.Repeater[RD.Partition][0], RepeaterSize) ;
    memcpy (&Window[WallSize + RepeaterSize], &TD.Repeater[RD.Partition][0], RepeaterSize) ;

    // Execute until we enter the repeater
    while (TapeHead < (int)WallSize)
      {
      // Execute a step
      uint8_t Cell = Window[TapeHead] ;
      const Transition& S = TM[State][Cell] ;
      Window[TapeHead] = S.Write ;
      if (S.Move)
        {
        TapeHead-- ;
        if (TapeHead < 0) return MoveRepeaterIntoWall (InitialTape, TD, true) ;
        }
      else TapeHead++ ;
      State = S.Next ;

      WallTr.nSteps++ ;
      }
    WallTr.Final.Tape = ustring (Window.begin(), Window.begin() + WallSize) ;
    WallTr.Final.State = State ;
    WallTr.Final.TapeHead = WallSize ;

    RepTr.Initial.State = State ;
    RepTr.nSteps = 0 ;
    RepTr.Initial.Tape = Window ;
    RepTr.Initial.TapeHead = WallSize ;

    // Execute the repeater
    int Leftmost = TapeHead ;
    int Rightmost = TapeHead ;
    while (RepTr.nSteps < RD.RepeaterPeriod)
      {
      // Execute a step
      uint8_t Cell = Window[TapeHead] ;
      const Transition& S = TM[State][Cell] ;
      Window[TapeHead] = S.Write ;

      if (TapeHead > Rightmost) Rightmost = TapeHead ;

      if (S.Move)
        {
        TapeHead-- ;
        if (TapeHead < Leftmost)
          {
          if (TapeHead < 0) return MoveRepeaterIntoWall (InitialTape, TD, true) ;
          Leftmost = TapeHead ;
          }
        }
      else
        {
        TapeHead++ ;
        if (TapeHead >= (int)WindowSize) return MoveRepeaterIntoWall (InitialTape, TD, false) ;
        }
      State = S.Next ;

      RepTr.nSteps++ ;
      }

    // Trim RepTr.Initial to size
    RepTr.Initial.TapeHead -= Leftmost ;
    RepTr.Initial.Tape.erase (RepTr.Initial.Tape.begin() + Rightmost + 1, RepTr.Initial.Tape.end()) ;
    RepTr.Initial.Tape.erase (RepTr.Initial.Tape.begin(), RepTr.Initial.Tape.begin() + Leftmost) ;

    // Copy Window to RepTr.Final
    RepTr.Final.Tape = ustring (Window.begin() + Leftmost, Window.begin() + Rightmost + 1) ;
    RepTr.Final.State = State ;
    RepTr.Final.TapeHead = TapeHead - Leftmost ;

    // If we don't end up at the start of the second repeater and in the same state,
    // we need to make Wall bigger
    if (State != RepTr.Initial.State ||
      RepTr.Final.TapeHead - RepTr.Initial.TapeHead != RD.RepeaterShift)
        return MoveRepeaterIntoWall (InitialTape, TD, false) ;

    // If RepTr.Final doesn't match with the destination wall, we need to make
    // the destination wall bigger
    const ustring& DestWall = TD.Wall[TD.TapeHeadWall + 1] ;
    if (RepTr.Final.Tape.size() - RepTr.Final.TapeHead > DestWall.size())
      return MoveRepeaterIntoWall (InitialTape, TD, false) ;
    for (int i = 0 ; i < (int)RepTr.Final.Tape.size() - RepTr.Final.TapeHead ; i++)
      if (RepTr.Final.Tape[RepTr.Final.TapeHead + i] != DestWall[i])
        return MoveRepeaterIntoWall (InitialTape, TD, false) ;

    // If RepTr.Initial doen't follow on from RepTr.Final, we need to make Wall bigger
    if (!TestFollowOn (RepTr.Initial, RepTr.Final))
      return MoveRepeaterIntoWall (InitialTape, TD, false) ;
    }
  else // Right-to-left
    {
    if (i && WallSize == 0) return MoveRepeaterIntoWall (InitialTape, TD, false) ;
    int TapeHead = 2 * RepeaterSize + TD.TapeHeadOffset ;
    if (TapeHead >= (int)WindowSize) return MoveRepeaterIntoWall (InitialTape, TD, false) ;

    RDT.Partition-- ;

    memcpy (&Window[0], &TD.Repeater[RD.Partition][0], RepeaterSize) ;
    memcpy (&Window[RepeaterSize], &TD.Repeater[RD.Partition][0], RepeaterSize) ;
    memcpy (&Window[2 * RepeaterSize], &TD.Wall[TD.TapeHeadWall][0], WallSize) ;

    // Execute until we enter the repeater
    while (TapeHead >= (int)(2 * RepeaterSize))
      {
      // Execute a step
      uint8_t Cell = Window[TapeHead] ;
      const Transition& S = TM[State][Cell] ;
      Window[TapeHead] = S.Write ;
      if (S.Move) TapeHead-- ;
      else
        {
        TapeHead++ ;
        if (TapeHead >= (int)WindowSize) return MoveRepeaterIntoWall (InitialTape, TD, false) ;
        }
      State = S.Next ;

      WallTr.nSteps++ ;
      }
    WallTr.Final.Tape = ustring (Window.begin() + 2 * RepeaterSize, Window.end()) ;
    WallTr.Final.State = State ;
    WallTr.Final.TapeHead = -1 ;

    RepTr.Initial.State = State ;
    RepTr.nSteps = 0 ;
    RepTr.Initial.Tape = Window ;
    RepTr.Initial.TapeHead = 2 * RepeaterSize - 1 ;

    // Execute the repeater
    int Leftmost = TapeHead ;
    int Rightmost = TapeHead ;
    while (RepTr.nSteps < RD.RepeaterPeriod)
      {
      // Execute a step
      uint8_t Cell = Window[TapeHead] ;
      const Transition& S = TM[State][Cell] ;
      Window[TapeHead] = S.Write ;

      if (TapeHead < Leftmost) Leftmost = TapeHead ;

      if (S.Move)
        {
        TapeHead-- ;
        if (TapeHead < 0) return MoveRepeaterIntoWall (InitialTape, TD, true) ;
        }
      else
        {
        TapeHead++ ;
        if (TapeHead > Rightmost)
          {
          if (TapeHead >= (int)WindowSize) return MoveRepeaterIntoWall (InitialTape, TD, false) ;
          Rightmost = TapeHead ;
          }
        }
      State = S.Next ;

      RepTr.nSteps++ ;
      }

    // Trim RepTr.Initial to size
    RepTr.Initial.Tape.erase (RepTr.Initial.Tape.begin() + Rightmost + 1, RepTr.Initial.Tape.end()) ;
    RepTr.Initial.Tape.erase (RepTr.Initial.Tape.begin(), RepTr.Initial.Tape.begin() + Leftmost) ;
    RepTr.Initial.TapeHead -= Leftmost ;

    // Copy Window to RepTr.Final
    RepTr.Final.Tape = ustring (Window.begin() + Leftmost, Window.begin() + Rightmost + 1) ;
    RepTr.Final.State = State ;
    RepTr.Final.TapeHead = TapeHead - Leftmost ;

    // If we don't end up at the start of the second repeater and in the same state,
    // we need to make Wall bigger
    if (State != RepTr.Initial.State ||
      RepTr.Final.TapeHead - RepTr.Initial.TapeHead != RD.RepeaterShift)
        return MoveRepeaterIntoWall (InitialTape, TD, true) ;

    // If RepTr.Final doesn't match with the destination wall, we need to make
    // the destination wall bigger
    const ustring& DestWall = TD.Wall[TD.TapeHeadWall - 1] ;
    if (RepTr.Final.TapeHead >= (int)DestWall.size()) return MoveRepeaterIntoWall (InitialTape, TD, true) ;
    for (int i = 0 ; i < RepTr.Final.TapeHead ; i++)
      if (RepTr.Final.Tape[i] != DestWall[DestWall.size() - RepTr.Final.TapeHead + i])
        return MoveRepeaterIntoWall (InitialTape, TD, true) ;

    // If RepTr.Initial doen't follow on from RepTr.Final, we need to make Wall bigger
    if (!TestFollowOn (RepTr.Initial, RepTr.Final))
      return MoveRepeaterIntoWall (InitialTape, TD, true) ;
    }

  return ORA_Result::OK ;
  }

void BouncerDecider::CheckTransitions (TapeDescriptor& InitialTape, uint32_t i,
  TapeDescriptor& TD, const RunDataTransitions& RDT)
  {
  //
  // WallTransition
  //

  uint32_t WallIndex = TD.TapeHeadWall ;
  ustring& Wall = TD.Wall[WallIndex] ;
  const SegmentTransition& WallTr = RDT.WallTransition ;
  const SegmentTransition& RepTr = RDT.RepeaterTransition ;

  int Shift = RepTr.Final.TapeHead - RepTr.Initial.TapeHead ;
  if (Shift == 0) TM_ERROR() ;

  // Check that WallTr is a valid transition
  CheckTransition (WallTr) ;

  // Check that WallTr.Initial matches TD
  if (WallTr.Initial.Tape != Wall) TM_ERROR() ;
  if (WallTr.Initial.TapeHead != TD.TapeHeadOffset) TM_ERROR() ;
  if (WallTr.Initial.State != TD.State) TM_ERROR() ;

  // Update TD according to WallTr
  Wall = WallTr.Final.Tape ;
  TD.TapeHeadOffset = WallTr.Final.TapeHead ;
  TD.State = WallTr.Final.State ;

  // Check that WallTr ends at the first repeater
  if (Shift > 0) // Left-to-right
    {
    if (WallTr.Final.TapeHead != (int)Wall.size()) TM_ERROR() ;
    }
  else
    {
    if (WallTr.Final.TapeHead != -1) TM_ERROR() ;
    }

  if (i == 0) InitialTape = TD ;

  //
  // RepeaterTransition
  //

  // Check that RepTr is a valid transition
  CheckTransition (RepTr) ;

  // Preliminary checks
  if (TD.State != RepTr.Initial.State) TM_ERROR() ;
  if (RepTr.Initial.State != RepTr.Final.State)
    TM_ERROR() ;

  // Check the inductive step
  if (Shift > 0) // Left-to-right
    {
    ustring& Rep = TD.Repeater[WallIndex] ;
    ustring& DestWall = TD.Wall[WallIndex + 1] ;

    // Check that RepTr advances the tape head by Stride cells
    int Stride = Rep.size() ;
    if (Stride != RepTr.Final.TapeHead - RepTr.Initial.TapeHead)
      TM_ERROR() ;

    // Check that RepTr doesn't extend beyond the start of the current wall
    if (RepTr.Initial.TapeHead > (int)Wall.size()) TM_ERROR() ;

    // Check that RepTr doesn't extend beyond the end of the destination wall
    if (RepTr.Initial.Tape.size() > RepTr.Final.TapeHead + DestWall.size()) TM_ERROR() ;

    // Decompose RepTr.Initial.Tape as A || Rep || B
    size_t lenA = RepTr.Initial.TapeHead ;
    ustring A (RepTr.Initial.Tape.begin(), RepTr.Initial.Tape.begin() + lenA) ;
    size_t lenB = RepTr.Initial.Tape.size() - lenA - Stride ;
    ustring B (RepTr.Initial.Tape.end() - lenB, RepTr.Initial.Tape.end()) ;

    // Check that the current wall ends with A
    if (Wall.size() < lenA) TM_ERROR() ;
    for (size_t i = 0 ; i < lenA ; i++)
      if (A[i] != Wall[i + Wall.size() - lenA]) TM_ERROR() ;

    // Check that Rep starts with B
    if (Rep.size() < lenB) TM_ERROR() ;
    for (size_t i = 0 ; i < lenB ; i++)
      if (Rep[i] != B[i]) TM_ERROR() ;

    // Check that the middle section matches Rep
    for (int i = 0 ; i < Stride ; i++)
      if (RepTr.Initial.Tape[i + lenA] != Rep[i]) TM_ERROR() ;

    // Check that the destination wall starts with B
    if (DestWall.size() < lenB) TM_ERROR() ;
    for (size_t i = 0 ; i < lenB ; i++)
      if (B[i] != DestWall[i]) TM_ERROR() ;

    // Remove A from Wall
    Wall.resize (Wall.size() - lenA) ;

    // Replace Rep with initial section of RepTr.Final.Tape
    Rep = ustring (RepTr.Final.Tape.begin(), RepTr.Final.Tape.begin() + Stride) ;

    // Remove B from Dest
    DestWall.erase (DestWall.begin(), DestWall.begin() + lenB) ;

    // Insert final section of RepTr.Final.Tape in destination wall
    DestWall.insert (DestWall.begin(), RepTr.Final.Tape.begin() + Stride, RepTr.Final.Tape.end()) ;

    TD.TapeHeadOffset = RepTr.Final.TapeHead - Stride ;
    TD.TapeHeadWall = WallIndex + 1 ;
    }
  else // Right-to-left
    {
    ustring& Rep = TD.Repeater[WallIndex - 1] ;
    ustring& DestWall = TD.Wall[WallIndex - 1] ;

    // Check that RepTr reduces the tape head by Stride cells
    int Stride = Rep.size() ;
    if (Stride != RepTr.Initial.TapeHead - RepTr.Final.TapeHead)
      TM_ERROR() ;

    // Check that RepTr.Initial doesn't extend beyond the end of the current wall
    if ((int)RepTr.Initial.Tape.size() - RepTr.Initial.TapeHead >
      (int)Wall.size() - TD.TapeHeadOffset) TM_ERROR() ;

    // Check that RepTr doesn't extend beyond the end of the destination wall
    if (RepTr.Final.TapeHead > (int)DestWall.size()) TM_ERROR() ;

    // Decompose RepTr.Initial.Tape as A || Rep || B
    size_t lenA = RepTr.Initial.TapeHead - Stride + 1 ;
    ustring A (RepTr.Initial.Tape.begin(), RepTr.Initial.Tape.begin() + lenA) ;
    size_t lenB = RepTr.Initial.Tape.size() - lenA - Stride ;
    ustring B (RepTr.Initial.Tape.end() - lenB, RepTr.Initial.Tape.end()) ;

    // Check that the current wall starts with B
    if (Wall.size() < lenB) TM_ERROR() ;
    for (size_t i = 0 ; i < lenB ; i++)
      if (Wall[i] != B[i]) TM_ERROR() ;

    // Check that Rep ends with A
    if (Stride < (int)lenA) TM_ERROR() ;
    for (size_t i = 0 ; i < lenA ; i++)
      if (Rep[Stride - lenA + i] != A[i])
        TM_ERROR() ;

    // Check that the middle section matches Rep
    for (int i = 0 ; i < Stride ; i++)
      if (RepTr.Initial.Tape[i + lenA] != Rep[i]) TM_ERROR() ;

    // Check that the destination wall ends with A
    if (DestWall.size() < lenA) TM_ERROR() ;
    for (size_t i = 0 ; i < lenA ; i++)
      if (A[i] != DestWall[i + DestWall.size() - lenA]) TM_ERROR() ;

    // Remove B from Wall
    Wall.erase (Wall.begin(), Wall.begin() + lenB) ;

    // Replace Rep with final section of RepTr.Final.Tape
    Rep = ustring (RepTr.Final.Tape.end() - Stride, RepTr.Final.Tape.end()) ;

    // Remove A from Dest
    DestWall.erase (DestWall.end() - lenA, DestWall.end()) ;

    TD.TapeHeadOffset = DestWall.size() + RepTr.Final.TapeHead ;
    TD.TapeHeadWall = WallIndex - 1 ;

    // Append initial section of RepTr.Final.Tape to destination wall
    DestWall.insert (DestWall.end(), RepTr.Final.Tape.begin(), RepTr.Final.Tape.end() - Stride) ;
    }

  TD.State = RepTr.Final.State ;
  }

BouncerDecider::ORA_Result BouncerDecider::MoveRepeaterIntoWall (TapeDescriptor& InitialTape,
  const TapeDescriptor& TD, bool RightHandEnd)
  {
  if (RightHandEnd)
    {
    if (TD.TapeHeadWall == 0) TM_ERROR() ;
    ustring& Rep = InitialTape.Repeater[TD.TapeHeadWall - 1] ;
    InitialTape.Wall[TD.TapeHeadWall].insert (InitialTape.Wall[TD.TapeHeadWall].begin(),
      Rep.begin(), Rep.end()) ;
    if (TD.TapeHeadWall == InitialTape.TapeHeadWall) InitialTape.TapeHeadOffset += Rep.size() ;
    if (--InitialTape.RepeaterCount[TD.TapeHeadWall - 1] < 3) return ORA_Result::ABORT ;
    }
  else
    {
    if (TD.TapeHeadWall == nPartitions) TM_ERROR() ;
    ustring& Rep = InitialTape.Repeater[TD.TapeHeadWall] ;
    InitialTape.Wall[TD.TapeHeadWall].insert (InitialTape.Wall[TD.TapeHeadWall].end(),
      Rep.begin(), Rep.end()) ;
    if (--InitialTape.RepeaterCount[TD.TapeHeadWall] < 3) return ORA_Result::ABORT ;
    }
  return ORA_Result::RETRY ;
  }
