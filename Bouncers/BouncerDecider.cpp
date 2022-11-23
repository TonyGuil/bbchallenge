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

  for (uint32_t i = 0 ; i < 1000 ; i++)
    if (Step() != StepResult::OK) return false ;
  int RightRecord = Rightmost ;
  int LeftRecord = Leftmost ;
  Type = BouncerType::Unknown ;

  while (StepCount < TimeLimit)
    {
    if (TapeHead > RightRecord)
      {
      if (Tape[TapeHead] == TAPE_SENTINEL) return false ;
      if (nRightRecords == RecordLimit) return false ;
      RightRecordList[nRightRecords].StepCount = StepCount ;
      RightRecordList[nRightRecords].TapeHead = TapeHead ;
      RightRecordList[nRightRecords].Prev = LatestRightRecord[State] ; ;
      LatestRightRecord[State] = &RightRecordList[nRightRecords] ;
      nRightRecords++ ;
      RightRecord = TapeHead ;
      if (DetectRepetition (LatestRightRecord, State, VerificationEntry))
        {
        if (TraceOutput) printf ("%d\n", SeedDatabaseIndex) ;
        return true ;
        }
      }
    if (TapeHead < LeftRecord)
      {
      if (Tape[TapeHead] == TAPE_SENTINEL) return false ;
      if (nLeftRecords == RecordLimit) return false ;
      LeftRecordList[nLeftRecords].StepCount = StepCount ;
      LeftRecordList[nLeftRecords].TapeHead = TapeHead ;
      LeftRecordList[nLeftRecords].Prev = LatestLeftRecord[State] ; ;
      LatestLeftRecord[State] = &LeftRecordList[nLeftRecords] ;
      nLeftRecords++ ;
      LeftRecord = TapeHead ;
      if (DetectRepetition (LatestLeftRecord, State, VerificationEntry))
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
        printf ("Unexpected HALT state reached! %d\n", StepCount) ;
        printf ("SeedIndex = %d, TapeHead = %d\n", Load32 (MachineSpec - 4), TapeHead) ;
        exit (1) ;
      }
    }

  if (Type == BouncerType::Bell) nBells++ ;

  return false ;
  }

bool BouncerDecider::DetectRepetition (Record* LatestRecord[], uint8_t State, uint8_t* VerificationEntry)
  {
  #define BACKWARD_SCAN_LENGTH 10
  Record* Workspace[4 * BACKWARD_SCAN_LENGTH] ;
  Record* Latest = LatestRecord[State] ;

  for (int i = 1 ; i <= BACKWARD_SCAN_LENGTH ; i++)
    {
    for (int j = 0 ; j < 4 ; j++)
      {
      if (Latest == 0) return false ;
      Workspace[4 * (i - 1) + j] = Latest ;
      Latest = Latest -> Prev ;
      }

    CycleShift = Workspace[0] -> TapeHead - Workspace[i] -> TapeHead ;
    if (Workspace[i] -> TapeHead - Workspace[2 * i] -> TapeHead != CycleShift
      || Workspace[2 * i] -> TapeHead - Workspace[3 * i] -> TapeHead != CycleShift)
        continue ;

    if (QuadraticProgression (Workspace[0] -> StepCount, Workspace[i] -> StepCount,
      Workspace[2 * i] -> StepCount, Workspace[3 * i] -> StepCount))
        {
        GetRepetitionParams (Workspace[2 * i] -> StepCount,
          Workspace[i] -> StepCount, Workspace[0] -> StepCount) ;
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
        if (!MakeRunDescriptor()) continue ;
        if (!CheckRuns())
          TM_ERROR() ;

        if (TraceOutput)
          {
          DumpRunData() ;
          DumpTransitions() ;
          }

        uint32_t n = RunDataArray[0].Repeater - ConfigWorkspace ;
        for (uint32_t i = 0 ; i < n ; i++) Step() ;
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
        int FinalLeftmost = std::min (Cycle2Leftmost, InitialLeftmost) ;
        int FinalRightmost = std::max (Cycle2Rightmost, InitialRightmost) ;

        MakeTranslatedBouncerData() ;

        int TapeLeftmost = std::min (Leftmost, Cycle2Leftmost) ;
        int TapeRightmost = std::max (Rightmost, Cycle2Rightmost) ;

        // If AnalyseTape finds a repeater sequence that is too short,
        // it simply decreases RepeaterCount and requests a retry:
        int Retries = 0 ;

TryAgain:

        if (Retries >= 3) TM_ERROR() ;
        if (Retries)
          {
          // After changing RepeaterCount, we must recompute the RunDescriptor
          if (!MakeRunDescriptor()) TM_ERROR() ;
          if (!CheckRuns()) TM_ERROR() ;
          }

        Retries++ ;

        *Clone = *this ;
        if (!AnalyseTape (Clone, InitialTape, 0, TapeLeftmost, TapeRightmost))
          goto TryAgain ;
        CheckTape (Clone, InitialTape) ;

        uint32_t pVerif = 12 ;
        if (VerificationEntry)
          {
          VerificationEntry[pVerif++] = (uint8_t)Type ;
          VerificationEntry[pVerif++] = nPartitions ;
          VerificationEntry[pVerif++] = nRuns ;
          pVerif += Save32 (VerificationEntry + pVerif, StepCount) ;
          pVerif += Save32 (VerificationEntry + pVerif, InitialLeftmost) ;
          pVerif += Save32 (VerificationEntry + pVerif, InitialRightmost) ;
          pVerif += Save32 (VerificationEntry + pVerif, StepCount + StepCount2) ;
          pVerif += Save32 (VerificationEntry + pVerif, FinalLeftmost) ;
          pVerif += Save32 (VerificationEntry + pVerif, FinalRightmost) ;
          for (uint32_t j = 0 ; j < nPartitions ; j++)
            pVerif += Save16 (VerificationEntry + pVerif, PartitionDataArray[j].RepeaterCount) ;
          pVerif += WriteTapeDescriptor (VerificationEntry + pVerif, Clone, InitialTape) ;
          }

        TapeDescriptor TD0 (this), TD1 (InitialTape) ;

        for (uint32_t i = 0 ; i < nRuns ; i++)
          {
          const RunData& RD = RunDataArray[i] ;

          for (uint32_t j = 0 ; j < RD.RepeaterSteps ; j++)
            if (Clone -> Step() != StepResult::OK) TM_ERROR() ;
          if (!AnalyseTape (Clone, TD0, i, TapeLeftmost, TapeRightmost))
            goto TryAgain ;
          CheckTape (Clone, TD0) ;
          CheckRepeaterTransition (TD1, TD0, RunDescriptorArray[i].RepeaterTransition) ;

          if (VerificationEntry)
            {
            VerificationEntry[pVerif++] = RD.Partition ;
            pVerif += Save16 (VerificationEntry + pVerif, RD.RepeaterCount) ;
            pVerif += WriteTransition (VerificationEntry + pVerif,
              RunDescriptorArray[i].RepeaterTransition) ;
            pVerif += WriteTapeDescriptor (VerificationEntry + pVerif, Clone, TD0) ;
            }

          for (uint32_t j = 0 ; j < RD.WallSteps ; j++)
            if (Clone -> Step() != StepResult::OK) TM_ERROR() ;
          if (!AnalyseTape (Clone, TD1, i, TapeLeftmost, TapeRightmost))
            goto TryAgain ;
          CheckTape (Clone, TD1) ;
          if (i < nRuns - 1)
            {
            if (RemoveGap (TD1, RunDescriptorArray[i + 1].RepeaterTransition))
              CheckTape (Clone, TD1) ;
            if (TruncateWall (TD1, RunDescriptorArray[i + 1].RepeaterTransition))
              CheckTape (Clone, TD1) ;
            }
          CheckWallTransition (TD0, TD1, RunDescriptorArray[i].WallTransition) ;

          if (VerificationEntry)
            {
            pVerif += WriteTransition (VerificationEntry + pVerif,
              RunDescriptorArray[i].WallTransition) ;
            pVerif += WriteTapeDescriptor (VerificationEntry + pVerif, Clone, TD1) ;
            }
          }

        FinalTape = TD1 ;

        // Generate a wraparound RunData entry
        RunData& RDFirst = RunDataArray[0] ;
        RunData& RDLast = RunDataArray[nRuns] ;
        RDLast = RDFirst ;
        RDLast.Repeater = RunDataArray[nRuns - 1].Wall + RunDataArray[nRuns - 1].WallSteps ;

        if (!AnalyseTape (Clone, FinalTape, nRuns, TapeLeftmost, TapeRightmost))
          goto TryAgain ;
        CheckTape (Clone, FinalTape) ;
        CheckWallTransition (TD0, FinalTape, RunDescriptorArray[nRuns - 1].WallTransition) ;

        // Write the info length
        if (VerificationEntry) Save32 (VerificationEntry + 8, pVerif - 12) ;

//        if ((int)pVerif > MaxStat)
//          {
//          MaxStat = pVerif ;
//          MaxStatMachine = SeedDatabaseIndex ;
//          }

        if (TraceOutput) PrintTape (TD1) ;

        for (uint32_t i = 0 ; i < nPartitions ; i++)
          InitialTape.Wall[i].insert (InitialTape.Wall[i].end(),
            InitialTape.Repeater[i].begin(), InitialTape.Repeater[i].end()) ;
        if (LeftmostShift > 0)
          {
          InitialTape.Leftmost -= LeftmostShift ;
          ExpandTapeLeftward (FinalTape, LeftmostShift) ;
          }
        if (RightmostShift > 0)
          {
          InitialTape.Rightmost += RightmostShift ;
          ExpandTapeRightward (FinalTape, RightmostShift) ;
          }

        CheckTapesEquivalent (InitialTape, FinalTape) ;

        CheckTape (Clone, FinalTape) ;

        if (TraceOutput)
          {
          printf ("\n") ;
          PrintTape (FinalTape) ;
          }

        if (nRuns > nRunsMax)
          {
          nRunsMax = nRuns ;
          nRunsMachine = SeedDatabaseIndex ;
          }

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
    }

  return false ;
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
  R.Wall = Cycle2 ;

  // Find the number of matching steps
  uint32_t MatchLen ;
  for (MatchLen = 0 ; ; MatchLen++)
    if (Cycle1[MatchLen] != Cycle2[MatchLen]) break ;
  if (Cycle2[MatchLen].State == 0) // Wrapped around
    {
    R.Wall = Cycle2 ;
    R.WallSteps = MatchLen ;
    R.Repeater = 0 ;
    return true ;
    }

  // Look for repeaters
  // Look for repeaters
  //
  // A repeater is _acceptable_ if it is repeated at least six times
  // (including the first), and it covers at least 24 steps or MatchLen/2 steps,
  // whichever is smaller. We can't just look for the acceptable repeater with
  // the highest repeat count, because a run may have more than one repeat run:
  //
  //   W0 n0*R0 W1 n1*R1 W2
  //
  // In this case we want to give priority to R0, even if n1 > n0. So we reject
  // R1 if it starts after the R0 run:

  R.RepeaterSteps = R.RepeaterPeriod = 0 ;
  R.Repeater = 0 ; // i.e. none found yet
  uint32_t MaxRepeaterPeriod = MatchLen / 4 ;
  uint32_t MinRepeaterSteps = std::min ((uint32_t)24, MatchLen / 2) ;
  for (uint32_t RepeaterPeriod = 1 ; RepeaterPeriod < MaxRepeaterPeriod ; RepeaterPeriod++)
    {
    uint32_t p = RepeaterPeriod ;
    uint32_t RepeaterStart = 0 ;
    for ( ; ; )
      {
      while (Cycle2[p] == Cycle2[p - RepeaterPeriod]) p++ ;
      if (p - RepeaterStart >= MinRepeaterSteps)
        {
        uint32_t RepeaterCount = (p - RepeaterStart) / RepeaterPeriod ;
        if (RepeaterCount >= 6)
          {
          // This repeater is acceptable. Does it start to the right of the
          // best found so far?
          if (R.Repeater == 0 || Cycle2 + RepeaterStart < R.Repeater + R.RepeaterSteps)
            {
            // No. Does it have a higher repeat count?
            if (R.Repeater == 0 || RepeaterCount * R.RepeaterPeriod > R.RepeaterSteps)
              {
              // Yes, so save it as the best
              R.WallSteps = RepeaterStart ;
              R.Repeater = Cycle2 + RepeaterStart ;
              R.RepeaterPeriod = RepeaterPeriod ;
              R.RepeaterSteps = p - RepeaterStart ;
              }
            }
          }
        }
      while (p < MatchLen && Cycle2[p] != Cycle2[p - RepeaterPeriod]) p++ ;
      if (p >= MatchLen) break ;
      RepeaterStart = p - RepeaterPeriod ;
      }
    }

  if (R.Repeater == 0) return false ;

  if (R.Repeater + R.RepeaterSteps < Cycle2 + MatchLen)
    {
    // The repeater didn't cover the entire match length, so we have a
    // non-expanding run
    R.WallSteps = R.Repeater - Cycle2 ;
    R.Expanding = false ;
    R.Direction = (R.Repeater[R.RepeaterSteps].TapeHead > R.Wall[0].TapeHead) ? 1 : -1 ;
    return false ; // for now
////return true ;
    }

  R.Expanding = true ;

  // Cycle2 should have a whole number of repeated segments before it matches Cycle1 again
  uint32_t Diff = R.WallSteps + R.RepeaterSteps ;
  if (Diff <= MatchLen) return false ;
  Diff -= MatchLen ;
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

bool BouncerDecider::MakeRunDescriptor()
  {
  // Convert all the RunData to RunDescriptors
  for (uint32_t i = 0 ; i < nRuns ; i++)
    ConvertRunData (RunDescriptorArray[i], RunDataArray[i]) ;
  return true ;
  }

void BouncerDecider::ConvertRunData (RunDescriptor& To, const RunData& From)
  {
  To.Partition = From.Partition ;
  To.Direction = From.Direction ;
  To.Expanding = From.Expanding ;

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
    if (To.RepeaterTransition.Initial.Tape.at(TapeHead) == TAPE_ANY)
      To.RepeaterTransition.Initial.Tape.at(TapeHead) = C.Cell ;
    To.RepeaterTransition.Final.Tape.at(TapeHead) = TM[C.State][C.Cell].Write ;
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
    if (To.WallTransition.Initial.Tape.at(TapeHead) == TAPE_ANY)
      To.WallTransition.Initial.Tape.at(TapeHead) = C.Cell ;
    To.WallTransition.Final.Tape.at(TapeHead) = TM[C.State][C.Cell].Write ;
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

  // Check the RunDescriptor array
  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    RunDescriptor& SD = RunDescriptorArray[i] ;
    RunDescriptor& NextSD = RunDescriptorArray[(i + 1) % nRuns] ;

    CheckTransition (SD.RepeaterTransition) ;
    CheckTransition (SD.WallTransition) ;

    if (!MatchSegments (SD.RepeaterTransition.Final, SD.RepeaterTransition.Initial))
      TM_ERROR() ;
    if (SD.WallTransition.Initial.Tape.empty())
      {
      if (!MatchSegments (SD.RepeaterTransition.Final, NextSD.RepeaterTransition.Initial))
        TM_ERROR() ;
      }
    else
      {
      if (!MatchSegments (SD.RepeaterTransition.Final, SD.WallTransition.Initial))
        TM_ERROR() ;
      if (!MatchSegments (SD.WallTransition.Final, NextSD.RepeaterTransition.Initial))
        TM_ERROR() ;
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

void BouncerDecider::PrintTransition (const Transition& Tr)
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
    printf ("%d", Tr.Initial.Tape.at(i)) ;
    printf (i == Tr.Initial.TapeHead - 1 ? "[" : i == Tr.Initial.TapeHead ? "]" : " ") ;
    }

  printf (" -> %c: ", Tr.Final.State + '@') ;
  if (Tr.Final.TapeHead == -1) printf ("[ ]") ;
  else if (Tr.Final.TapeHead == 0) printf ("[") ;
  for (i = 0 ; i < (int)Tr.Final.Tape.size() ; i++)
    {
    printf ("%d", Tr.Final.Tape.at(i)) ;
    printf (i == Tr.Final.TapeHead - 1 ? "[" : i == Tr.Final.TapeHead ? "]" : " ") ;
    }
  if (i == Tr.Final.TapeHead) printf (" ]") ;
  printf ("\n") ;
  }

bool BouncerDecider::AnalyseTape (const TuringMachine* TM, TapeDescriptor& TD,
  uint32_t Run, int Leftmost, int Rightmost)
  {
  TD.State = TM -> State ;
  TD.Leftmost = Leftmost ;
  TD.Rightmost = Rightmost ;

  int WallLeftmost[MAX_PARTITIONS + 1] ;
  int WallRightmost[MAX_PARTITIONS + 1] ;
  for (uint32_t i = 0 ; i <= nPartitions ; i++)
    {
    WallLeftmost[i] = INT_MAX ;
    WallRightmost[i] = INT_MIN ;
    }
  WallLeftmost[0] = Leftmost ;
  WallRightmost[nPartitions] = Rightmost ;

  // Handle Translated Bouncer dummy partition if present
  switch (TB_Direction)
    {
    case 1:
      TD.Wall[0] = TB_Wall ;
      TD.Repeater[0] = TB_Repeater ;
      TD.RepeaterCount[0] = TB_RepeaterCount ;
      WallRightmost[0] = Leftmost + TB_Wall.size() - 1 ;
      WallLeftmost[1] = Leftmost + TB_Size ;
      break ;

    case -1:
      TD.Wall[nPartitions] = TB_Wall ;
      TD.Repeater[nPartitions - 1] = TB_Repeater ;
      TD.RepeaterCount[nPartitions - 1] = TB_RepeaterCount ;
      WallRightmost[nPartitions - 1] = Rightmost - TB_Size ;
      WallLeftmost[nPartitions] = Rightmost - TB_Wall.size() + 1 ;
      break ;

    case 0: break ;
    default: TM_ERROR() ;
    }

  // For each partition, find the first wall that feeds into that partition, and
  // the repeating sequence that it initiates
  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    uint32_t RDIndex = Run + i ;
    if (Run != nRuns || i != 0) RDIndex %= nRuns ;
    const RunData& RD = RunDataArray[RDIndex] ;

    if (WallRightmost[RD.Partition] != INT_MIN) continue ;

    PartitionData& PD = PartitionDataArray[RD.Partition] ;
    TD.RepeaterCount[RD.Partition] = PD.RepeaterCount ;
    TD.Repeater[RD.Partition].resize (PD.RepeaterShift) ;

    int RepeaterLeft = RD.Repeater[0].TapeHead ;
    int RepeaterRight = RD.Repeater[0].TapeHead ;
    for (uint32_t j = 0 ; j < RD.RepeaterPeriod ; j++)
      {
      if (RD.Repeater[j].TapeHead < RepeaterLeft)
        RepeaterLeft = RD.Repeater[j].TapeHead ;
      if (RD.Repeater[j].TapeHead > RepeaterRight)
        RepeaterRight = RD.Repeater[j].TapeHead ;
      }

    if (RD.Direction == -1)
      {
      if (RepeaterRight >= WallLeftmost[RD.Partition + 1])
        RepeaterRight = WallLeftmost[RD.Partition + 1] - 1 ;
      if (WallRightmost[RD.Partition + 1] != INT_MIN &&
        RepeaterRight > WallRightmost[RD.Partition + 1])
          RepeaterRight = WallRightmost[RD.Partition + 1] ;
      if (RepeaterLeft > RepeaterRight) RepeaterLeft = RepeaterRight ;

      // Look for a sequence of RepeaterCount segments of length PD.RepeaterShift
      int SequenceStart = RepeaterRight ;
      int SequenceEnd = SequenceStart - PD.RepeaterShift ;
      int SequenceLen = PD.RepeaterCount * PD.RepeaterShift ;
      for ( ; ; )
        {
        while (TM -> Tape[SequenceEnd] != TM -> Tape[SequenceEnd + PD.RepeaterShift])
          SequenceEnd-- ;
        SequenceStart = SequenceEnd + PD.RepeaterShift ;
        while (SequenceEnd > SequenceStart - SequenceLen &&
          TM -> Tape[SequenceEnd] == TM -> Tape[SequenceEnd + PD.RepeaterShift])
            SequenceEnd-- ;
        if (SequenceEnd == SequenceStart - SequenceLen)
          {
          memcpy (&TD.Repeater[RD.Partition][0],
            &TM -> Tape[SequenceStart - PD.RepeaterShift + 1], PD.RepeaterShift) ;
          break ;
          }
        SequenceStart = SequenceEnd + PD.RepeaterShift ;
        if (SequenceStart < RepeaterLeft - PD.RepeaterShift)
          goto DecrementRepeaterCount ; // Failed -- decrement RepeaterCount and request a retry
        }
      WallRightmost[RD.Partition] = SequenceEnd ;
      WallLeftmost[RD.Partition + 1] = SequenceStart + 1 ;
      }
    else
      {
      if (RepeaterLeft <= WallRightmost[RD.Partition])
        RepeaterLeft = WallRightmost[RD.Partition] + 1 ;
      if (WallLeftmost[RD.Partition] != INT_MAX &&
        RepeaterLeft < WallLeftmost[RD.Partition])
          RepeaterLeft = WallLeftmost[RD.Partition] ;
      if (RepeaterRight < RepeaterLeft) RepeaterRight = RepeaterLeft ;

      // Look for a sequence of RepeaterCount segments of length PD.RepeaterShift
      int SequenceStart = RepeaterLeft ;
      int SequenceEnd = SequenceStart + PD.RepeaterShift ;
      int SequenceLen = PD.RepeaterCount * PD.RepeaterShift ;
      for ( ; ; )
        {
        while (TM -> Tape[SequenceEnd] != TM -> Tape[SequenceEnd - PD.RepeaterShift])
          SequenceEnd++ ;
        SequenceStart = SequenceEnd - PD.RepeaterShift ;
        while (SequenceEnd < SequenceStart + SequenceLen &&
          TM -> Tape[SequenceEnd] == TM -> Tape[SequenceEnd - PD.RepeaterShift])
            SequenceEnd++ ;
        if (SequenceEnd == SequenceStart + SequenceLen)
          {
          memcpy (&TD.Repeater[RD.Partition][0], &TM -> Tape[SequenceStart], PD.RepeaterShift) ;
          break ;
          }
        SequenceStart = SequenceEnd - PD.RepeaterShift ;
        if (SequenceStart > RepeaterRight + PD.RepeaterShift)
          goto DecrementRepeaterCount ; // Failed -- decrement RepeaterCount and request a retry
        }
      WallRightmost[RD.Partition] = SequenceStart - 1 ;
      WallLeftmost[RD.Partition + 1] = SequenceEnd ;
      }
    continue ;

DecrementRepeaterCount:

    if (PD.RepeaterCount < 5) TM_ERROR() ;
    PD.RepeaterCount-- ;

    // Adjust all runs in this partition
    for (uint32_t j = 0 ; j < nRuns ; j++)
      {
      RunData& R = RunDataArray[j] ;
      if (R.Partition == RD.Partition)
        {
        R.RepeaterCount-- ;
        R.RepeaterSteps -= R.RepeaterPeriod ;
        R.Wall -= R.RepeaterPeriod ;
        R.WallSteps += R.RepeaterPeriod ;
        }
      }
    return false ; // will get retried
    }

  for (uint32_t Partition = 0 ; Partition <= nPartitions ; Partition++)
    {
    if (WallLeftmost[Partition] > WallRightmost[Partition] + 1)
      TM_ERROR() ;
    uint32_t WallLen = WallRightmost[Partition] - WallLeftmost[Partition] + 1 ;
    TD.Wall[Partition].resize (WallLen) ;
    memcpy (&TD.Wall[Partition][0], &TM -> Tape[WallLeftmost[Partition]], WallLen) ;
    }

  // Find the wall nearest to the tape head
  uint32_t ShortestDistance = INT_MAX ;
  for (uint32_t i = 0 ; i <= nPartitions ; i++)
    {
    uint32_t Distance ;
    if (TM -> TapeHead < WallLeftmost[i])
      Distance = WallLeftmost[i] - TM -> TapeHead ;
    else if (TM -> TapeHead > WallRightmost[i])
      Distance = TM -> TapeHead - WallRightmost[i] ;
    else
      {
      TD.TapeHeadWall = i ;
      break ;
      }
    if (Distance < ShortestDistance)
      {
      ShortestDistance = Distance ;
      TD.TapeHeadWall = i ;
      }
    }
  TD.TapeHeadOffset = TM -> TapeHead - WallLeftmost[TD.TapeHeadWall] ;

  return true ;
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

uint32_t BouncerDecider::WriteTransition (uint8_t* VerificationEntry, const Transition& Tr) const
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

void BouncerDecider::MakeTranslatedBouncerData()
  {
  if (TB_Direction == 1) // Translation to the right
    {
    // We expect a repeater sequence starting from Cycle1Leftmost and moving left
    TB_Size = Cycle1Leftmost - Leftmost ;
    uint32_t Shift = Cycle2Leftmost - Cycle1Leftmost ;
    TB_Repeater = std::vector<uint8_t> (Shift) ;
    memcpy (&TB_Repeater[0], Tape + Cycle1Leftmost - Shift, Shift) ;
    uint32_t RepeaterLen ;
    for (RepeaterLen = Shift + 1 ; (int)RepeaterLen <= Cycle1Leftmost - Leftmost ; RepeaterLen++)
      if (Tape[Cycle1Leftmost - RepeaterLen] != Tape[Cycle1Leftmost - RepeaterLen + Shift])
        break ;
    RepeaterLen-- ;
    PartitionData& PD = PartitionDataArray[0] ;
    PD.RepeaterShift = Shift ;
    TB_RepeaterCount = PD.RepeaterCount = RepeaterLen / Shift ;
    if (PD.RepeaterCount < 3) TM_ERROR() ;
    RepeaterLen = PD.RepeaterCount * Shift ;

    TB_Wall = std::vector<uint8_t> (Cycle1Leftmost - Leftmost - RepeaterLen) ;
    memcpy (&TB_Wall[0], Tape + Leftmost, TB_Wall.size()) ;
    }
  else if (TB_Direction == -1) // Translation to the left
    {
    // We expect a repeater sequence starting from Cycle1Rightmost and moving right
    TB_Size = Rightmost - Cycle1Rightmost ;
    uint32_t Shift = Cycle1Rightmost - Cycle2Rightmost ;
    TB_Repeater = std::vector<uint8_t> (Shift) ;
    memcpy (&TB_Repeater[0], Tape + Cycle1Rightmost + 1, Shift) ;
    uint32_t RepeaterLen ;
    for (RepeaterLen = Shift + 1 ; (int)RepeaterLen <= Rightmost - Cycle1Rightmost ; RepeaterLen++)
      if (Tape[Cycle1Rightmost + RepeaterLen] != Tape[Cycle1Rightmost + RepeaterLen - Shift])
        break ;
    RepeaterLen-- ;
    PartitionData& PD = PartitionDataArray[nPartitions - 1] ;
    PD.RepeaterShift = Shift ;
    TB_RepeaterCount = PD.RepeaterCount = RepeaterLen / Shift ;
    if (PD.RepeaterCount < 3) TM_ERROR() ;
    RepeaterLen = PD.RepeaterCount * Shift ;

    TB_Wall = std::vector<uint8_t> (Rightmost - Cycle1Rightmost - RepeaterLen) ;
    memcpy (&TB_Wall[0], Tape + Cycle1Rightmost, TB_Wall.size()) ;
    }
  }

// bool BouncerDecider::RemoveGap (TapeDescriptor TD, const Transition& Tr)
//
// If there is a gap between the wall and Tr.Initial.Tape, close it by adding
// Repeaters to the current wall, and removing them from the destination wall
//
// Returns true if any gap was removed

bool BouncerDecider::RemoveGap (TapeDescriptor& TD, const Transition& Tr)
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

    // Check that the destination wall ends with copies of the Repeater
    int Rotate = Gap % Stride ; Rotate = Stride - Rotate ; Rotate %= Stride ;
    if ((int)TD.Wall[Wall - 1].size() < Gap) TM_ERROR() ;
    for (int i = 0 ; i < Gap ; i++)
      if (TD.Wall[Wall - 1].at(i + TD.Wall[Wall - 1].size() - Gap) !=
        TD.Repeater[Wall - 1].at((i + Rotate) % Stride))
          TM_ERROR() ;

    // Prepend Repeaters to the current wall
    TD.Wall[Wall].insert (TD.Wall[Wall].begin(), Gap, 0) ;
    for (int i = 0 ; i < Gap ; i++)
      TD.Wall[Wall].at(i) = TD.Repeater[Wall - 1].at((i + Rotate) % Stride) ;

    // Remove them from the destination wall
    TD.Wall[Wall - 1].erase (TD.Wall[Wall - 1].end() - Gap, TD.Wall[Wall - 1].end()) ;

    // Rotate the Repeater accordingly
    std::vector<uint8_t> Repeater = TD.Repeater[Wall - 1] ;
    for (uint32_t i = 0 ; i < Repeater.size() ; i++)
      TD.Repeater[Wall - 1].at(i) = Repeater.at((i + Rotate) % Stride) ;

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

    // Check that the destination wall starts with copies of the Repeater
    if ((int)TD.Wall[Wall + 1].size() < Gap) TM_ERROR() ;
    for (int i = 0 ; i < Gap ; i++)
      if (TD.Wall[Wall + 1].at(i) != TD.Repeater[Wall].at(i % Stride)) TM_ERROR() ;

    // Append Repeaters to the current wall
    for (int i = 0 ; i < Gap ; i++)
      TD.Wall[Wall].push_back (TD.Repeater[Wall].at(i % Stride)) ;

    // Remove them from the destination wall
    TD.Wall[Wall + 1].erase (TD.Wall[Wall + 1].begin(), TD.Wall[Wall + 1].begin() + Gap) ;

    // Rotate the Repeater accordingly
    std::vector<uint8_t> Repeater = TD.Repeater[Wall] ;
    for (uint32_t i = 0 ; i < Repeater.size() ; i++)
      TD.Repeater[Wall].at(i) = Repeater.at((i + Gap) % Stride) ;
    }
  return true ;
  }

// bool BouncerDecider::TruncateWall (TapeDescriptor& TD, const Transition& Tr)
//
// Ensure that Tr.Initial.Tape extends at least as far as TD.Wall,
// by truncating the wall if necessary

bool BouncerDecider::TruncateWall (TapeDescriptor& TD, const Transition& Tr)
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
      if (TD.Wall[Wall].at(i) != TD.Repeater[Wall - 1].at(i % Stride))
        TM_ERROR() ;
  
    // Re-align the wall so that the repeaters start immediately to the left of Tr.Initial.Tape
    TD.Wall[Wall - 1].insert (TD.Wall[Wall - 1].end(),
      TD.Wall[Wall].begin(), TD.Wall[Wall].begin() + Overhang) ;
    TD.Wall[Wall].erase (TD.Wall[Wall].begin(), TD.Wall[Wall].begin() + Overhang) ;
  
    // Rotate the Repeater accordingly
    std::vector<uint8_t> Repeater = TD.Repeater[Wall - 1] ;
    for (uint32_t i = 0 ; i < Repeater.size() ; i++)
      TD.Repeater[Wall - 1].at(i) = Repeater.at((i + Overhang) % Stride) ;
  
    // This rotated repeater should be an initial segment of Tr.Initial.Tape
    for (uint32_t i = 0 ; i < Repeater.size() ; i++)
      if (TD.Repeater[Wall - 1].at(i) != Tr.Initial.Tape.at(i))
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
      if (TD.Wall[Wall].at(i) != TD.Repeater[Wall].at(t))
        TM_ERROR() ;
      }

    // Re-align the walls so that the repeaters start immediately after Tr.Initial.Tape
    TD.Wall[Wall + 1].insert (TD.Wall[Wall + 1].begin(),
      TD.Wall[Wall].begin() + InitOffset + Tr.Initial.Tape.size(),
        TD.Wall[Wall].end()) ;
    TD.Wall[Wall].resize (InitOffset + Tr.Initial.Tape.size()) ;

    // Rotate the Repeater accordingly
    std::vector<uint8_t> Repeater = TD.Repeater[Wall] ;
    for (uint32_t i = 0 ; i < Repeater.size() ; i++)
      TD.Repeater[Wall].at((i + Overhang) % Stride) = Repeater.at(i) ;

    // This rotated repeater should be a final segment of Tr.Initial.Tape
    for (uint32_t i = 0 ; i < Repeater.size() ; i++)
      if (TD.Repeater[Wall].at(i) != Tr.Initial.Tape.at(Tr.Initial.Tape.size() - Stride + i))
        TM_ERROR() ;
    }
  return true ;
  }
