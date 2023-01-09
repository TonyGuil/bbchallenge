#include "BouncerVerifier.h"

void BouncerVerifier::Verify (uint32_t SeedDatabaseIndex, const uint8_t* MachineSpec, FILE* fp)
  {
  Initialise (SeedDatabaseIndex, MachineSpec) ;

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

  uint32_t InitialSteps = Read32 (fp) ;
  int InitialLeftmost = Read32 (fp) ;
  int InitialRightmost = Read32 (fp) ;

  uint32_t FinalSteps = Read32 (fp) ;
  int FinalLeftmost = Read32 (fp) ;
  int FinalRightmost = Read32 (fp) ;
  TapeLeftmost = FinalLeftmost ;
  TapeRightmost = FinalRightmost ;

  // Run machine up to the start of the Cycle
  while (StepCount < InitialSteps)
    if (Step() != StepResult::OK) VERIFY_ERROR() ;
  if (Leftmost != InitialLeftmost || Rightmost != InitialRightmost)
    VERIFY_ERROR() ;

  for (uint32_t i = 0 ; i < nPartitions ; i++)
    RepeaterCount[i] = Read16u (fp) ;

  // Read the initial TapeDescriptor, and check that it matches our Tape
  ReadTapeDescriptor (fp, InitialTape) ;
  CheckTape (this, InitialTape) ;

  RunDescriptor RD (this) ;
  Segment PreviousSeg ;
  Segment FirstSeg ;
  TapeDescriptor PreviousTape = InitialTape ;
  for (uint32_t i = 0 ; i < nRuns ; i++)
    {
    ReadRunDescriptor (fp, RD) ;

    // Check that each Transition is compatible with its predecessor and follower.
    // This includes checking that a RepeaterTransition is compatible with itself.
    if (i == 0) FirstSeg = RD.RepeaterTransition.Initial ; // to be checked at the end
    else CheckFollowOn (PreviousSeg, RD.RepeaterTransition.Initial) ;
    CheckFollowOn (RD.RepeaterTransition.Final, RD.RepeaterTransition.Initial) ;
    if (RD.WallTransition.Initial.Tape.empty())
      PreviousSeg = RD.RepeaterTransition.Final ;
    else
      {
      CheckFollowOn (RD.RepeaterTransition.Final, RD.WallTransition.Initial) ;
      PreviousSeg = RD.WallTransition.Final ;
      }

    // Execute the Repeater steps and check that they produce the expected tape
    uint32_t RepeaterSteps = RD.RepeaterTransition.nSteps ;
    RepeaterSteps *= RepeaterCount[RD.Partition] ;
    for (uint32_t j = 0 ; j < RepeaterSteps ; j++)
      if (Step() != StepResult::OK) VERIFY_ERROR() ;
    CheckTape (this, RD.TD0) ;

    // Check that RD.RepeaterTransition converts previous tape into TD0
    CheckRepeaterTransition (PreviousTape, RD.TD0, RD.RepeaterTransition) ;

    // Execute the Wall steps and check that they produce the expected tape
    for (uint32_t j = 0 ; j < RD.WallTransition.nSteps ; j++)
      if (Step() != StepResult::OK) VERIFY_ERROR() ;
    CheckTape (this, RD.TD1) ;

    // Check that RD.WallTransition converts TD0 into TD1
    CheckWallTransition (RD.TD0, RD.TD1, RD.WallTransition) ;

    PreviousTape = RD.TD1 ;
    }

  // Check that the last Transition segment wraps around correctly to the first
  CheckFollowOn (PreviousSeg, FirstSeg) ;

  // Check FinalSteps, FinalLeftmost, and FinalRightmost
  if (StepCount != FinalSteps) VERIFY_ERROR() ;
  if (Leftmost != FinalLeftmost || Rightmost != FinalRightmost) VERIFY_ERROR() ;

  // Augmenting all the walls in the InitialTape by appending a single copy
  // of the repeater should result in the final tape (now in PreviousTape)
  for (uint32_t i = 0 ; i < nPartitions ; i++)
    InitialTape.Wall[i].insert (InitialTape.Wall[i].end(),
      InitialTape.Repeater[i].begin(), InitialTape.Repeater[i].end()) ;

  // Pad out PreviousTape to match the new version of InitialTape
  int LeftmostShift = InitialLeftmost - FinalLeftmost ;
  int RightmostShift = FinalRightmost - InitialRightmost ;
  PreviousTape.Wall[0].insert (PreviousTape.Wall[0].begin(), LeftmostShift, 0) ;
  PreviousTape.Wall[nPartitions].resize (PreviousTape.Wall[nPartitions].size() + RightmostShift) ;

  // Re-calibrate the tape boundaries
  InitialTape.Leftmost = PreviousTape.Leftmost = FinalLeftmost - LeftmostShift ;
  InitialTape.Rightmost = PreviousTape.Rightmost = FinalRightmost + RightmostShift ;

  // Adjust TapeHeadOffset
  if (InitialTape.TapeHeadWall == 0) PreviousTape.TapeHeadOffset += LeftmostShift ;

  // Now the two tapes should be equivalent
  CheckTapesEquivalent (InitialTape, PreviousTape) ;

  // Check that the InfoLength field was correct
  if (ftell (fp) != StartPos + InfoLength) VERIFY_ERROR() ;
  }

void BouncerVerifier::ReadRunDescriptor (FILE* fp, RunDescriptor& RD)
  {
  RD.Partition = Read8u (fp) ;
  ReadTransition (fp, RD.RepeaterTransition) ;
  ReadTapeDescriptor (fp, RD.TD0) ;
  ReadTransition (fp, RD.WallTransition) ;
  ReadTapeDescriptor (fp, RD.TD1) ;
  }

void BouncerVerifier::ReadTransition (FILE* fp, Transition& Tr)
  {
  Tr.nSteps = Read16u (fp) ;
  ReadSegment (fp, Tr.Initial) ;
  ReadSegment (fp, Tr.Final) ;
  CheckTransition (Tr) ;
  }

void BouncerVerifier::ReadSegment (FILE* fp, Segment& Seg)
  {
  Seg.State = Read8u (fp) ;
  if (Seg.State < 1 || Seg.State > 5) VERIFY_ERROR() ;
  Seg.TapeHead = Read16s (fp) ;
  ReadByteArray (fp, Seg.Tape) ;
  }

void BouncerVerifier::ReadTapeDescriptor (FILE* fp, TapeDescriptor& TD)
  {
  TD.Leftmost = TapeLeftmost ;
  TD.Rightmost = TapeRightmost ;

  memcpy (TD.RepeaterCount, RepeaterCount, nPartitions * sizeof (uint32_t)) ;

  TD.State = Read8u (fp) ;
  if (TD.State < 1 || TD.State > 5) VERIFY_ERROR() ;
  TD.TapeHeadWall = Read8u (fp) ;
  TD.TapeHeadOffset = Read16s (fp) ;
  for (uint32_t i = 0 ; i <= nPartitions ; i++)
    ReadByteArray (fp, TD.Wall[i]) ;
  for (uint32_t i = 0 ; i < nPartitions ; i++)
    ReadByteArray (fp, TD.Repeater[i]) ;
  }

void BouncerVerifier::ReadByteArray (FILE* fp, std::vector<uint8_t>& Data)
  {
  uint32_t Len = Read16u (fp) ;
  Data.clear() ;
  Data.resize (Len) ;
  if (Len && fread (&Data[0], Len, 1, fp) != 1) VERIFY_ERROR() ;
  }
