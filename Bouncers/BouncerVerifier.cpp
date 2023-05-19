#include "BouncerVerifier.h"

void BouncerVerifier::Verify (FILE* fp)
  {
  int InfoLength = Read32 (fp) ;
  long StartPos = ftell (fp) ;

  // Check bouncer type
  Type = (BouncerType)Read8u (fp) ;
  if ((uint32_t)Type < (uint32_t)BouncerType::Unilateral ||
    (uint32_t)Type > (uint32_t)BouncerType::Translated) VERIFY_ERROR() ;

  nPartitions = Read8u (fp) ;
  if (nPartitions > MAX_PARTITIONS) VERIFY_ERROR() ;

  nRuns = Read16u (fp) ;
  if (nRuns > MAX_RUNS) VERIFY_ERROR() ;

  // Read initial stats
  uint32_t InitialSteps = Read32 (fp) ;
  int InitialLeftmost = Read32 (fp) ;
  int InitialRightmost = Read32 (fp) ;

  // Read final stats
  uint32_t FinalSteps = Read32 (fp) ;
  FinalLeftmost = Read32 (fp) ;
  FinalRightmost = Read32 (fp) ;

  // Read the RepeaterCount array
  for (uint32_t i = 0 ; i < nPartitions ; i++)
    RepeaterCount[i] = Read16u (fp) ;

  // Run machine up to the start of the Cycle, and check the initial stats
  while (StepCount < InitialSteps)
    if (Step() != StepResult::OK) VERIFY_ERROR() ;
  if (Leftmost != InitialLeftmost || Rightmost != InitialRightmost)
    VERIFY_ERROR() ;

  // Read the initial TapeDescriptor and check that it describes the
  // initial machine configuration
  ReadTapeDescriptor (fp, InitialTape) ;
  CheckTape (this, InitialTape) ;

  RunDescriptor RD ;
  Segment PreviousSeg ;
  Segment FirstSeg ;
  TapeDescriptor CurrentTape = InitialTape ;
  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    // Read the next RunDescriptor
    ReadRunDescriptor (fp, RD) ;

    // Check that each SegmentTransition is compatible with its predecessor and follower.
    // This includes checking that a RepeaterTransition is compatible with itself:
    if (i == 0) FirstSeg = RD.RepeaterTransition.Initial ; // to be checked at the end
    else CheckFollowOn (PreviousSeg, RD.RepeaterTransition.Initial) ;
    CheckFollowOn (RD.RepeaterTransition.Final, RD.RepeaterTransition.Initial) ;
    CheckFollowOn (RD.RepeaterTransition.Final, RD.WallTransition.Initial) ;
    PreviousSeg = RD.WallTransition.Final ;

    // Execute the Repeater steps
    uint32_t RepeaterSteps = RD.RepeaterTransition.nSteps ;
    RepeaterSteps *= RepeaterCount[RD.Partition] ;
    for (uint32_t j = 0 ; j < RepeaterSteps ; j++)
      if (Step() != StepResult::OK) VERIFY_ERROR() ;

    // Update CurrentTape and check that it describes the current machine configuration
    CheckRepeaterTransition (RD.Partition, CurrentTape, RD.RepeaterTransition) ;
    CheckTape (this, CurrentTape) ;

    if (i == nRuns - 1)
      {
      if (CurrentTape.TapeHeadWall != 0 && CurrentTape.TapeHeadWall != nPartitions) VERIFY_ERROR() ;

      // The final wall might require adjustment to the repeater boundaries
      uint32_t FinalAdjustment = Read16u (fp) ;
      while (FinalAdjustment--)
        {
        if (CurrentTape.TapeHeadWall == 0)
          {
          // Add a repeater to the current wall
          CurrentTape.Wall[0] += CurrentTape.Repeater[0] ;

          // Check that the neighbouring wall contains a copy of the repeater, and remove it
          if (!CurrentTape.Wall[1].starts_with (CurrentTape.Repeater[0])) VERIFY_ERROR() ;
          CurrentTape.Wall[1].erase (0, CurrentTape.Repeater[0].size()) ;
          }
        else
          {
          // Add a repeater to the current wall
          CurrentTape.Wall[nPartitions].insert (0, CurrentTape.Repeater[nPartitions - 1]) ;
          uint32_t Stride = CurrentTape.Repeater[nPartitions - 1].size() ;
          CurrentTape.TapeHeadOffset += Stride ;

          // Check that the neighbouring wall contains a copy of the repeater, and remove it
          ustring& Neighbour = CurrentTape.Wall[nPartitions - 1] ;
          if (!Neighbour.ends_with (CurrentTape.Repeater[nPartitions - 1])) VERIFY_ERROR() ;
          Neighbour.resize (Neighbour.size() - Stride) ;
          }
        }
      }

    // Execute the Wall steps
    for (uint32_t j = 0 ; j < RD.WallTransition.nSteps ; j++)
      if (Step() != StepResult::OK) VERIFY_ERROR() ;

    // Update CurrentTape and check that it describes the current machine configuration
    CheckWallTransition (CurrentTape, RD.WallTransition, i == nRuns - 1) ;
    CheckTape (this, CurrentTape) ;
    }

  // Check that the last SegmentTransition segment wraps around correctly to the first
  CheckFollowOn (PreviousSeg, FirstSeg) ;

  // Check FinalSteps and Leftmost/Rightmost
  if (StepCount != FinalSteps) VERIFY_ERROR() ;
  if (Leftmost != FinalLeftmost || Rightmost != FinalRightmost)
    VERIFY_ERROR() ;

  // Check that CurrentTape is equivalent to the final tape from the verification file
  TapeDescriptor FinalTape (this) ;
  ReadTapeDescriptor (fp, FinalTape) ;
  CheckTapesEquivalent (FinalTape, CurrentTape) ;

  // Augmenting all the walls in the InitialTape by appending a single copy
  // of the repeater should result in the FinalTape
  for (uint32_t i = 0 ; i < nPartitions ; i++)
    InitialTape.Wall[i].insert (InitialTape.Wall[i].end(),
      InitialTape.Repeater[i].begin(), InitialTape.Repeater[i].end()) ;

  // Pad out FinalTape to match the new version of InitialTape
  int LeftmostShift = InitialLeftmost - FinalLeftmost ;
  int RightmostShift = FinalRightmost - InitialRightmost ;
  FinalTape.Wall[0].insert (FinalTape.Wall[0].begin(), LeftmostShift, 0) ;
  FinalTape.Wall[nPartitions].resize (FinalTape.Wall[nPartitions].size() + RightmostShift) ;

  // Re-calibrate the tape boundaries
  InitialTape.Leftmost = FinalTape.Leftmost = FinalLeftmost - LeftmostShift ;
  InitialTape.Rightmost = FinalTape.Rightmost = FinalRightmost + RightmostShift ;

  // Adjust TapeHeadOffset
  if (InitialTape.TapeHeadWall == 0)
    FinalTape.TapeHeadOffset += LeftmostShift ;

  // Now the two tapes should be equivalent...
  CheckTapesEquivalent (InitialTape, FinalTape) ;

  // ...and should represent the final machine state
  CheckTape (this, FinalTape) ;

  // Check that the InfoLength field was correct
  if (ftell (fp) != StartPos + InfoLength) VERIFY_ERROR() ;
  }

// void BouncerVerifier::CheckWallTransition (TapeDescriptor& TD, const SegmentTransition& Tr)
//
// Check that Tr.Initial matches TD, and update TD to match Tr.Final

void BouncerVerifier::CheckWallTransition (TapeDescriptor& TD, const SegmentTransition& Tr, bool Final)
  {
  // Check that Tr.Initial matches TD
  if (TD.State != Tr.Initial.State) TM_ERROR() ;
  ustring& Wall = TD.Wall[TD.TapeHeadWall] ;
  if (Wall != Tr.Initial.Tape) TM_ERROR() ;
  if (TD.TapeHeadOffset != Tr.Initial.TapeHead) TM_ERROR() ;

  // Update TD to match Tr.Final
  TD.State = Tr.Final.State ;
  Wall = Tr.Final.Tape ;
  TD.TapeHeadOffset = Tr.Final.TapeHead ;

  // Check that TapeHead has just left the wall to the left or the right
  if (!Final && TD.TapeHeadOffset != -1 && TD.TapeHeadOffset != (int)Wall.size()) TM_ERROR() ;
  }

// void BouncerVerifier::CheckRepeaterTransition (uint32_t Partition,
//   TapeDescriptor& TD, const SegmentTransition& Tr)
//
// Check that Tr.Initial matches TD, and update TD to match Tr.Final

void BouncerVerifier::CheckRepeaterTransition (uint32_t Partition,
  TapeDescriptor& TD, const SegmentTransition& Tr)
  {
  if (TD.State != Tr.Initial.State) TM_ERROR() ;

  ustring& Repeater = TD.Repeater[Partition] ;
  uint32_t Stride = Repeater.size() ;

  if (TD.TapeHeadOffset == -1)
    {
    // Right-to-left
    if (Partition != TD.TapeHeadWall - 1) TM_ERROR() ;
    if (Tr.Initial.TapeHead - Tr.Final.TapeHead != (int)Stride) TM_ERROR() ;

    // Decompose Tr.Initial.Tape into A || R || B
    ustring_view A (&Tr.Initial.Tape[0], Tr.Final.TapeHead + 1) ;
    ustring_view R (&Tr.Initial.Tape[Tr.Final.TapeHead + 1], Stride) ;
    ustring_view B (&Tr.Initial.Tape[Tr.Initial.TapeHead + 1],
      Tr.Initial.Tape.size() - Tr.Initial.TapeHead - 1) ;
    if (!Tr.Final.Tape.starts_with (A)) TM_ERROR() ;

   // B should match the left-hand end of the source wall
    ustring& SrcWall = TD.Wall[TD.TapeHeadWall] ;
    if (!SrcWall.starts_with (B)) TM_ERROR() ;
  
    // R should match the repeater
    if (R != Repeater) TM_ERROR() ;

    // A should match the right-hand end of the destination wall
    ustring& DestWall = TD.Wall[TD.TapeHeadWall - 1] ;
    if (!DestWall.ends_with (A)) TM_ERROR() ;
  
    // Remove B from source wall
    SrcWall.erase (0, B.size()) ;
  
    // Replace repeater with right-hand end of Tr.Final.Tape
    memcpy (&Repeater[0], &Tr.Final.Tape[Tr.Final.Tape.size() - R.size()], Stride) ;

    // Update TapeHead
    TD.TapeHeadWall-- ;
    TD.TapeHeadOffset = DestWall.size() - 1 ;

    // Append mid-portion of Tr.Final.Tape to destination wall
    DestWall.append (&Tr.Final.Tape[A.size()], B.size()) ;
    }
  else if (TD.TapeHeadOffset == (int)TD.Wall[TD.TapeHeadWall].size())
    {
    // Left-to-right
    if (Partition != TD.TapeHeadWall) TM_ERROR() ;
    if (Tr.Final.TapeHead - Tr.Initial.TapeHead != (int)Stride) TM_ERROR() ;

    // Decompose Tr.Initial.Tape into A || R || B
    ustring_view A (&Tr.Initial.Tape[0], Tr.Initial.TapeHead) ;
    ustring_view R (&Tr.Initial.Tape[Tr.Initial.TapeHead], Stride) ;
    ustring_view B (&Tr.Initial.Tape[Tr.Final.TapeHead],
      Tr.Initial.Tape.size() - Tr.Final.TapeHead) ;
    if (!Tr.Final.Tape.ends_with (B)) TM_ERROR() ;

    // A should match the right-hand end of the source wall
    ustring& SrcWall = TD.Wall[TD.TapeHeadWall] ;
    if (!SrcWall.ends_with (A)) TM_ERROR() ;
  
    // R should match the repeater
    if (R != Repeater) TM_ERROR() ;

    // B should match the left-hand end of the destination wall
    ustring& DestWall = TD.Wall[TD.TapeHeadWall + 1] ;
    if (!DestWall.starts_with (B)) TM_ERROR() ;
  
    // Remove A from source wall
    SrcWall.resize (SrcWall.size() - A.size()) ;
  
    // Replace repeater with left-hand end of Tr.Final.Tape
    memcpy (&Repeater[0], &Tr.Final.Tape[0], Stride) ;

    // Insert mid-portion of Tr.Final.Tape in destination wall
    DestWall.insert (0, &Tr.Final.Tape[Stride], A.size()) ;

    // Update TapeHead
    TD.TapeHeadWall++ ;
    TD.TapeHeadOffset = A.size() ;
    }
  else TM_ERROR() ; // Partition is invalid
  }

void BouncerVerifier::ReadRunDescriptor (FILE* fp, RunDescriptor& RD)
  {
  RD.Partition = Read8u (fp) ;
  ReadTransition (fp, RD.RepeaterTransition) ;
  ReadTransition (fp, RD.WallTransition) ;
  }

void BouncerVerifier::ReadTransition (FILE* fp, SegmentTransition& Tr)
  {
  Tr.nSteps = Read16u (fp) ;
  ReadSegment (fp, Tr.Initial) ;
  ReadSegment (fp, Tr.Final) ;
  CheckTransition (Tr) ;
  }

void BouncerVerifier::ReadSegment (FILE* fp, Segment& Seg)
  {
  Seg.State = Read8u (fp) ;
  if (Seg.State < 1 || Seg.State > MachineStates) VERIFY_ERROR() ;
  Seg.TapeHead = Read16s (fp) ;
  ReadByteArray (fp, Seg.Tape) ;
  }

void BouncerVerifier::ReadTapeDescriptor (FILE* fp, TapeDescriptor& TD)
  {
  memcpy (TD.RepeaterCount, RepeaterCount, nPartitions * sizeof (uint32_t)) ;

  TD.Leftmost = FinalLeftmost ;
  TD.Rightmost = FinalRightmost ;
  TD.State = Read8u (fp) ;
  if (TD.State < 1 || TD.State > MachineStates) VERIFY_ERROR() ;
  TD.TapeHeadWall = Read8u (fp) ;
  TD.TapeHeadOffset = Read16s (fp) ;
  for (uint32_t i = 0 ; i <= nPartitions ; i++)
    ReadByteArray (fp, TD.Wall[i]) ;
  for (uint32_t i = 0 ; i < nPartitions ; i++)
    ReadByteArray (fp, TD.Repeater[i]) ;
  }

void BouncerVerifier::ReadByteArray (FILE* fp, ustring& Data)
  {
  uint32_t Len = Read16u (fp) ;
  Data.clear() ;
  Data.resize (Len) ;
  Read (fp, &Data[0], Len) ;
  }

void BouncerVerifier::VerifyHalter (FILE* fp)
  {
  if (Read32 (fp) != 4) VERIFY_ERROR() ; // InfoLength
  uint32_t nSteps = Read32 (fp) ;
  while (--nSteps) if (Step() != StepResult::OK) VERIFY_ERROR() ;
  if (Step() != StepResult::HALT) VERIFY_ERROR() ;
  }
