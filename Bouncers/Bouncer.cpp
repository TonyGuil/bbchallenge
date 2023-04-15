// The Bouncer class contains functions and data that are used by both
// the Decider and the Verifier

#include "Bouncer.h"

// void Bouncer::CheckTape (const TuringMachine* TM, const TapeDescriptor& TD)
//
// Check that machine TM is faithfully described by TapeDescriptor TD

void Bouncer::CheckTape (const TuringMachine* TM, const TapeDescriptor& TD)
  {
  // Check state and TapeHeadWall
  if (TD.State != TM -> State) TM_ERROR() ;
  if (TD.TapeHeadWall > nPartitions) TM_ERROR() ;

  // Check that cells not described by TD are zero
  for (int i = TM -> Leftmost ; i < TD.Leftmost ; i++)
    if (TM -> Tape[i] != 0) TM_ERROR() ;
  for (int i = TD.Rightmost + 1 ; i <= TM -> Rightmost ; i++)
    if (TM -> Tape[i] != 0) TM_ERROR() ;

  // Check each cell
  TapePosition TP ;
  InitTapePosition (TD, TP) ;
  for (int i = TD.Leftmost ; i <= TD.Rightmost ; i++)
    if (NextCell (TD, TP, i) != TM -> Tape[i]) TM_ERROR() ;
  if (!TP.Finished || TP.WallOffset == INT_MIN) TM_ERROR() ;

  // Check TapeHead
  if (TP.WallOffset + TD.TapeHeadOffset != TM -> TapeHead) TM_ERROR() ;
  }

void Bouncer::CheckTransition (const SegmentTransition& Tr) const
  {
  // Initial and Final tapes must be the same size
  if (Tr.Initial.Tape.size() != Tr.Final.Tape.size()) TM_ERROR() ;

  if (Tr.Initial.Tape.empty())
    {
    // Special case
    if (Tr.nSteps != 0) TM_ERROR() ;
    if (Tr.Initial.TapeHead != 0 || Tr.Final.TapeHead != 0) TM_ERROR() ;
    return ;
    }

  // Initial tape head must be within the tape boundaries; final tape head
  // can be +/-1 either side
  if (Tr.Initial.TapeHead < 0 || Tr.Initial.TapeHead >= (int)Tr.Initial.Tape.size())
    TM_ERROR() ;
  if (Tr.Final.TapeHead < -1 || Tr.Final.TapeHead > (int)Tr.Final.Tape.size())
    TM_ERROR() ;

  // Check that executing nSteps transforms Initial to Final without
  // exceeding the tape boundaries
  auto Tape = Tr.Initial.Tape ;
  uint8_t State = Tr.Initial.State ;
  int TapeHead = Tr.Initial.TapeHead ;
  for (uint32_t i = 0 ; i < Tr.nSteps ; i++)
    {
    if (TapeHead < 0 || TapeHead >= (int)Tape.size()) TM_ERROR() ;

    uint8_t Cell = Tape[TapeHead] ;
    if (Cell > 1) TM_ERROR() ;
    const Transition& S = TM[State][Cell] ;
    Tape[TapeHead] = S.Write ;
    if (S.Move) TapeHead-- ;
    else TapeHead++ ;
    State = S.Next ;
    if (State < 1 || State > MachineStates) TM_ERROR() ;
    }

  // Now we should have reached Tr.Final
  if (State != Tr.Final.State) TM_ERROR() ;
  if (TapeHead != Tr.Final.TapeHead) TM_ERROR() ;
  if (Tape != Tr.Final.Tape) TM_ERROR() ;
  }

// void Bouncer::CheckFollowOn (const Segment& Seg1, const Segment& Seg2)
//
// Check that Seg2 follows on from Seg1:
//  - Seg2.State = Seg1.State;
//  - after aligning the tapes so that Tr1.Final.TapeHead and Tr2.Initial.TapeHead
//    are equal, the tapes agree with each other on the overlapping segment.

void Bouncer::CheckFollowOn (const Segment& Seg1, const Segment& Seg2)
  {
  if (Seg1.State != Seg2.State) TM_ERROR() ;

  if (Seg1.Tape.empty() || Seg2.Tape.empty()) return ;

  int Shift = Seg1.TapeHead - Seg2.TapeHead ;
  int Left = 0 ;
  int Right = Seg1.Tape.size() ;
  if (Shift > 0)
    {
    Left = Shift ;
    if (Left > (int)Seg1.Tape.size()) TM_ERROR() ;
    if (Right > int (Seg2.Tape.size() + Shift))
      Right = Seg2.Tape.size() + Shift ;
    }
  else
    {
    Right = Seg2.Tape.size() + Shift ;
    if (Right > (int)Seg1.Tape.size())
      Right = Seg1.Tape.size() ;
    }
  if (Left > Right) TM_ERROR() ;
  for (int i = Left ; i < Right ; i++)
    if ((Seg1.Tape[i] ^ Seg2.Tape[i - Shift]) == 1)
      TM_ERROR() ;
  }

// void Bouncer::CheckWallTransition (TapeDescriptor TD0,
//   TapeDescriptor TD1, const SegmentTransition& Tr)
//
// Check that Tr transforms tape TD0 into tape TD1
//
// Note that TD0 and TD1 are passed by value, not by reference; so we
// are free to modify them here

void Bouncer::CheckWallTransition (TapeDescriptor TD0,
  TapeDescriptor TD1, const SegmentTransition& Tr)
  {
  // Preliminary checks
  if (TD0.State != Tr.Initial.State) TM_ERROR() ;
  if (TD1.State != Tr.Final.State) TM_ERROR() ;
  for (uint32_t i = 0 ; i < nPartitions ; i++)
    {
    if (TD0.RepeaterCount[i] != TD1.RepeaterCount[i]) TM_ERROR() ;
    if (TD0.Repeater[i].size() != TD1.Repeater[i].size()) TM_ERROR() ;
    }
  uint32_t Len0 = 0, Len1 = 0 ;
  for (uint32_t i = 0 ; i <= nPartitions ; i++)
    {
    Len0 += TD0.Wall[i].size() ;
    Len1 += TD1.Wall[i].size() ;
    }
  if (Len0 != Len1) TM_ERROR() ;

  uint32_t Wall = TD0.TapeHeadWall ;
  if (TD1.TapeHeadWall != Wall) TM_ERROR() ;

  // Check that the SegmentTransition's initial configuration is compatible with the TD0 tape
  CheckSegment (TD0, Tr.Initial, Wall) ;

  // Check that the SegmentTransition's final configuration is compatible with the TD1 tape
  CheckSegment (TD1, Tr.Final, Wall) ;

  // Check that TD0, overwritten with Tr.Final.Tape, generates the
  // same tape contents as TD1
  TD0.State = Tr.Final.State ;

  // Expand the wall to the left in both TapeDescriptors so that
  // Tr.Initial.Tape doesn't spill over to the left
  int WallLeftmost = 0 ;
  int WallRightmost = TD0.Wall[Wall].size() ;
  int Shift = Tr.Initial.TapeHead - TD0.TapeHeadOffset ;
  WallLeftmost += Shift ;
  WallRightmost += Shift ;
  ExpandWallsLeftward (TD0, TD1, Wall, WallLeftmost) ;

  // Expand the wall to the right in both TapeDescriptors so that the
  // Tr.Final.Tape doesn't spill over to the right
  ExpandWallsRightward (TD0, TD1, Wall, Tr.Final.Tape.size() - WallRightmost) ;

  // Insert the SegmentTransition's final tape into TD0
  if (TD0.TapeHeadOffset < Tr.Initial.TapeHead) TM_ERROR() ;
  if (TD0.TapeHeadOffset - Tr.Initial.TapeHead + Tr.Final.Tape.size() > TD0.Wall[Wall].size())
    TM_ERROR() ;
  memcpy (&TD0.Wall[Wall][TD0.TapeHeadOffset - Tr.Initial.TapeHead],
    &Tr.Final.Tape[0], Tr.Final.Tape.size()) ;

  TD0.TapeHeadOffset += Tr.Final.TapeHead - Tr.Initial.TapeHead ;

  // Now TD0 and TD1 should define the same tape contents
  CheckTapesEquivalent (TD0, TD1) ;
  }

// void Bouncer::CheckRepeaterTransition (const TapeDescriptor& TD0,
//   const TapeDescriptor& TD1, const SegmentTransition& Tr)
//
// Check that Tr transforms tape TD0 into tape TD1

void Bouncer::CheckRepeaterTransition (const TapeDescriptor& TD0,
  const TapeDescriptor& TD1, const SegmentTransition& Tr)
  {
  // Preliminary checks
  if (TD0.State != Tr.Initial.State) TM_ERROR() ;
  if (TD1.State != Tr.Final.State) TM_ERROR() ;
  if (Tr.Initial.State != Tr.Final.State) TM_ERROR() ;
  for (uint32_t i = 0 ; i < nPartitions ; i++)
    {
    if (TD0.RepeaterCount[i] != TD1.RepeaterCount[i]) TM_ERROR() ;
    if (TD0.Repeater[i].size() != TD1.Repeater[i].size()) TM_ERROR() ;
    }
  uint32_t Len0 = 0, Len1 = 0 ;
  for (uint32_t i = 0 ; i <= nPartitions ; i++)
    {
    Len0 += TD0.Wall[i].size() ;
    Len1 += TD1.Wall[i].size() ;
    }
  if (Len0 != Len1)
    TM_ERROR() ;

  // Check that the SegmentTransition's initial configuration is compatible with the TD0 tape
  CheckSegment (TD0, Tr.Initial, TD0.TapeHeadWall) ;

  // Check that the SegmentTransition's final configuration is compatible with the TD1 tape
  CheckSegment (TD1, Tr.Final, TD1.TapeHeadWall) ;

  // Check the inductive step
  int Shift = Tr.Final.TapeHead - Tr.Initial.TapeHead ;
  if (Shift == 0) TM_ERROR() ;
  if (Shift < 0) CheckLeftwardRepeater (TD0, TD1, Tr) ;
  else CheckRightwardRepeater (TD0, TD1, Tr) ;
  }

// void Bouncer::CheckLeftwardRepeater (TapeDescriptor TD0, TapeDescriptor TD1, const SegmentTransition& Tr)
//
// Note that TD0 and TD1 are passed by value, not by reference; so we
// are free to modify them here

void Bouncer::CheckLeftwardRepeater (TapeDescriptor TD0, TapeDescriptor TD1, const SegmentTransition& Tr)
  {
  if (TD0.State != Tr.Initial.State) TM_ERROR() ;
  TD0.State = Tr.Final.State ;

  uint32_t WallIndex = TD0.TapeHeadWall ;
  std::vector<uint8_t>& Wall = TD0.Wall[WallIndex] ;
  std::vector<uint8_t>& Rep = TD0.Repeater[WallIndex - 1] ;

  // Check that Tr.Initial is compatible with the TD0 tape
  CheckSegment (TD0, Tr.Initial, WallIndex) ;

  // Check that Tr reduces the tape head by Stride cells
  int Stride = Rep.size() ;
  if (Stride != Tr.Initial.TapeHead - Tr.Final.TapeHead) TM_ERROR() ;

  // Check that Tr.Initial doesn't end to the right of the current wall
  if ((int)Tr.Initial.Tape.size() - Tr.Initial.TapeHead >
    (int)Wall.size() - TD0.TapeHeadOffset) TM_ERROR() ;

  // Check that Tr.Initial starts at the start of the first Repeater
  if (Tr.Initial.TapeHead != TD0.TapeHeadOffset + Stride) TM_ERROR() ;

  // Decompose Tr.Initial.Tape as Rep || A
  std::vector<uint8_t> A (Tr.Initial.Tape.begin() + Stride, Tr.Initial.Tape.end()) ;

  // Check that the current wall starts with A
  for (uint32_t i = 0 ; i < A.size() ; i++)
    if (Wall[i] != A[i]) TM_ERROR() ;

  // Check that Tr.Initial.Tape starts with Rep
  for (int i = 0 ; i < Stride ; i++)
    if (Tr.Initial.Tape[i] != Rep[i]) TM_ERROR() ;

  // Remove A from Wall
  Wall.erase (Wall.begin(), Wall.begin() + A.size()) ;

  // Replace Rep with the last Stride bytes of Tr.Final.Tape
  Rep = std::vector<uint8_t> (Tr.Final.Tape.end() - Stride, Tr.Final.Tape.end()) ;

  TD0.TapeHeadOffset = TD0.Wall[WallIndex - 1].size() + Tr.Final.TapeHead ;
  TD0.TapeHeadWall = WallIndex - 1 ;

  // Apppend the start of Tr.Final.Tape ('B' in the write-up) to the destination wall
  TD0.Wall[WallIndex - 1].insert (TD0.Wall[WallIndex - 1].end(),
    Tr.Final.Tape.begin(), Tr.Final.Tape.end() - Stride) ;

  // Now TD0 and TD1 should match
  CheckTapesEquivalent (TD0, TD1) ;
  }

// void Bouncer::CheckRightwardRepeater (TapeDescriptor TD0, TapeDescriptor TD1, const SegmentTransition& Tr)
//
// Note that TD0 and TD1 are passed by value, not by reference; so we
// are free to modify them here

void Bouncer::CheckRightwardRepeater (TapeDescriptor TD0, TapeDescriptor TD1, const SegmentTransition& Tr)
  {
  if (TD0.State != Tr.Initial.State) TM_ERROR() ;
  TD0.State = Tr.Final.State ;

  uint32_t WallIndex = TD0.TapeHeadWall ;
  std::vector<uint8_t>& Wall = TD0.Wall[WallIndex] ;
  std::vector<uint8_t>& Rep = TD0.Repeater[WallIndex] ;

  // Check that Tr.Initial is compatible with the TD0 tape
  CheckSegment (TD0, Tr.Initial, WallIndex) ;

  // Check that Tr advances the tape head by Stride cells
  int Stride = Rep.size() ;
  if (Stride != Tr.Final.TapeHead - Tr.Initial.TapeHead) TM_ERROR() ;

  // Check that Tr.Initial doesn't start to the left of the current wall
  if (Tr.Initial.TapeHead > TD0.TapeHeadOffset) TM_ERROR() ;

  // Check that Tr.Initial ends at the end of the first Repeater
  if (Tr.Initial.Tape.size() - Tr.Initial.TapeHead !=
    Wall.size() - TD0.TapeHeadOffset + Stride) TM_ERROR() ;

  // Decompose Tr.Initial.Tape as A || Rep
  std::vector<uint8_t> A (Tr.Initial.Tape.begin(), Tr.Initial.Tape.end() - Stride) ;

  // Check that the current wall ends with A
  for (uint32_t i = 0 ; i < A.size() ; i++)
    if (Wall[i + Wall.size() - A.size()] != A[i]) TM_ERROR() ;

  // Check that Tr.Initial.Tape ends with Rep
  for (uint32_t i = A.size() ; i < Tr.Initial.Tape.size() ; i++)
    if (Tr.Initial.Tape[i] != Rep[i - A.size()]) TM_ERROR() ;

  // Remove A from Wall
  //
  // Wall.resize (Wall.size() - A.size()) ;
  //
  // This triggers a spurious compiler warning with -std=c++20/c++23:
  //   warning: 'void* __builtin_memset(void*, int, long long unsigned int)'
  //   specified bound between 18446744069414584320 and 18446744073709551614 exceeds
  //   maximum object size 9223372036854775807 [-Wstringop-overflow=]
  //
  // So...
  Wall.resize (uint32_t(Wall.size() - A.size())) ;

  // Replace Rep with the first Stride bytes of Tr.Final.Tape
  Rep = std::vector<uint8_t> (Tr.Final.Tape.begin(), Tr.Final.Tape.begin() + Stride) ;

  // Prepend the rest of Tr.Final.Tape ('B' in the write-up) to the destination wall
  TD0.Wall[WallIndex + 1].insert (TD0.Wall[WallIndex + 1].begin(),
    Tr.Final.Tape.begin() + Stride, Tr.Final.Tape.end()) ;

  TD0.TapeHeadOffset = Tr.Initial.TapeHead ;
  TD0.TapeHeadWall = WallIndex + 1 ;

  // Now TD0 and TD1 should match
  CheckTapesEquivalent (TD0, TD1) ;
  }

// void Bouncer::ExpandWallsLeftward (TapeDescriptor& TD0, TapeDescriptor& TD1, uint32_t Wall, int Amount)
//
// Move the final repeaters from a sequence in the two tapes, by adding them to the wall

void Bouncer::ExpandWallsLeftward (TapeDescriptor& TD0, TapeDescriptor& TD1, uint32_t Wall, int Amount)
  {
  if (Amount <= 0) return ;
  if (Wall == 0) TM_ERROR() ;

  // Round up to a whole number of repeaters
  int DeltaCount = Amount + TD0.Repeater[Wall - 1].size() - 1 ;
  DeltaCount /= TD0.Repeater[Wall - 1].size() ;
  Amount = DeltaCount * TD0.Repeater[Wall - 1].size() ;

  if (TD0.RepeaterCount[Wall - 1] != TD1.RepeaterCount[Wall - 1] ||
    TD0.Repeater[Wall - 1].size() != TD1.Repeater[Wall - 1].size())
      TM_ERROR() ;

  // Check that we are not encroaching on the neighbouring wall
  if ((int)TD0.RepeaterCount[Wall - 1] < DeltaCount) TM_ERROR() ;

  TD0.RepeaterCount[Wall - 1] -= DeltaCount ;
  TD1.RepeaterCount[Wall - 1] -= DeltaCount ;
  while (DeltaCount)
    {
    TD0.Wall[Wall].insert (TD0.Wall[Wall].begin(), TD0.Repeater[Wall - 1].begin(),
      TD0.Repeater[Wall - 1].end()) ;
    TD1.Wall[Wall].insert (TD1.Wall[Wall].begin(), TD1.Repeater[Wall - 1].begin(),
      TD1.Repeater[Wall - 1].end()) ;
    DeltaCount-- ;
    }

  if (Wall == TD0.TapeHeadWall) TD0.TapeHeadOffset += Amount ;
  if (Wall == TD1.TapeHeadWall) TD1.TapeHeadOffset += Amount ;
  }

// void Bouncer::ExpandWallsRightward (TapeDescriptor& TD0, TapeDescriptor& TD1, uint32_t Wall, int Amount)
//
// Move the initial repeaters from a sequence in the two tapes, by adding them to the wall

void Bouncer::ExpandWallsRightward (TapeDescriptor& TD0, TapeDescriptor& TD1, uint32_t Wall, int Amount)
  {
  if (Amount <= 0) return ;
  if (Wall == nPartitions) TM_ERROR() ;

  // Round up to a whole number of repeaters
  int DeltaCount = Amount + TD0.Repeater[Wall].size() - 1 ;
  DeltaCount /= TD0.Repeater[Wall].size() ;

  if (TD0.RepeaterCount[Wall] != TD1.RepeaterCount[Wall] ||
    TD0.Repeater[Wall].size() != TD1.Repeater[Wall].size())
      TM_ERROR() ;

  // Check that we are not encroaching on the neighbouring wall
  if ((int)TD0.RepeaterCount[Wall] < DeltaCount) TM_ERROR() ;

  TD0.RepeaterCount[Wall] -= DeltaCount ;
  TD1.RepeaterCount[Wall] -= DeltaCount ;
  while (DeltaCount)
    {
    TD0.Wall[Wall].insert (TD0.Wall[Wall].end(), TD0.Repeater[Wall].begin(),
      TD0.Repeater[Wall].end()) ;
    TD1.Wall[Wall].insert (TD1.Wall[Wall].end(), TD1.Repeater[Wall].begin(),
      TD1.Repeater[Wall].end()) ;
    DeltaCount-- ;
    }
  }

void Bouncer::ExpandTapeLeftward (TapeDescriptor& TD, int Amount)
  {
  if (Amount < 0) TM_ERROR() ;
  if (Amount == 0) return ;

  TD.Wall[0].insert (TD.Wall[0].begin(), Amount, 0) ;
  TD.Leftmost -= Amount ;
  if (TD.TapeHeadWall == 0) TD.TapeHeadOffset += Amount ;
  }

void Bouncer::ExpandTapeRightward (TapeDescriptor& TD, int Amount)
  {
  if (Amount < 0) TM_ERROR() ;
  if (Amount == 0) return ;

  TD.Wall[nPartitions].resize (TD.Wall[nPartitions].size() + Amount) ;
  TD.Rightmost += Amount ;
  }

void Bouncer::CheckSegment (const TapeDescriptor& TD, const Segment& Seg, uint32_t Wall)
  {
  // Set up three tape regions: repeaters to the left, wall, and repeaters to the right:
  int RepeaterLeftmost = 0 ;
  if (Wall)
    RepeaterLeftmost = -(TD.RepeaterCount[Wall - 1] * TD.Repeater[Wall - 1].size()) ;
  int WallLeftmost = 0 ;
  int WallRightmost = TD.Wall[Wall].size() ;
  int RepeaterRightmost = WallRightmost ;
  if (Wall != nPartitions)
    RepeaterRightmost += TD.RepeaterCount[Wall] * TD.Repeater[Wall].size() ;
  int Shift = Seg.TapeHead - TD.TapeHeadOffset ;
  RepeaterLeftmost += Shift ;
  WallLeftmost += Shift ;
  WallRightmost += Shift ;
  RepeaterRightmost += Shift ;

  // Check that Segment's tape doesn't overflow into a neighbouring wall
  if (RepeaterLeftmost > 0) TM_ERROR() ;
  if (RepeaterRightmost < (int)Seg.Tape.size()) TM_ERROR() ;

  // Scan the three tape regions, aligning them with the Segment's tape
  int i ;
  for (i = 0 ; i < (int)Seg.Tape.size() && i < WallLeftmost ; i++)
    if (Seg.Tape[i] != TD.Repeater[Wall - 1][(i - RepeaterLeftmost) % TD.Repeater[Wall - 1].size()])
      TM_ERROR() ;

  for ( ; i < (int)Seg.Tape.size() && i < WallRightmost ; i++)
    if (Seg.Tape[i] != TD.Wall[Wall][i - WallLeftmost]) TM_ERROR() ;

  for ( ; i < (int)Seg.Tape.size() ; i++)
    if (Seg.Tape[i] != TD.Repeater[Wall][(i - WallRightmost) % TD.Repeater[Wall].size()])
      TM_ERROR() ;
  }

// void Bouncer::CheckTapesEquivalent (const TapeDescriptor& TD0, const TapeDescriptor& TD1)
//
// Checks that two TapeDescriptors define the same tape
//
// In fact it checks that the two TapeDescriptors will still define the same
// tape if we increase their RepeaterCounts by the same amount in each. This
// is necessary for the Verifier's inductive proof to go through.

void Bouncer::CheckTapesEquivalent (const TapeDescriptor& TD0, const TapeDescriptor& TD1) const
  {
  if (TD0.State != TD1.State) TM_ERROR() ;
  if (TD0.TapeHeadWall != TD1.TapeHeadWall) TM_ERROR() ;

  if (TD0.Leftmost != TD1.Leftmost) TM_ERROR() ;
  if (TD0.Rightmost != TD1.Rightmost) TM_ERROR() ;

  // Check that the repeater parameters match
  for (uint32_t i = 0 ; i < nPartitions ; i++)
    {
    if (TD0.RepeaterCount[i] != TD1.RepeaterCount[i]) TM_ERROR() ;
    if (TD0.Repeater[i].size() != TD1.Repeater[i].size()) TM_ERROR() ;
    if (TD0.RepeaterCount[i] < 1 || TD0.Repeater[i].size() == 0) TM_ERROR() ;
    }

  // Check that the repeater segments overlap by at least the length of one
  // repeater (this ensures that we can insert more repeaters into the segment
  // and the two descriptors will still be equivalent)
  int TapeHead0 = TD0.Leftmost ;
  int TapeHead1 = TD1.Leftmost ;
  for (uint32_t i = 0 ; i < nPartitions ; i++)
    {
    TapeHead0 += TD0.Wall[i].size() ;
    TapeHead1 += TD1.Wall[i].size() ;
    int Overlap = std::max (TapeHead0, TapeHead1) ;
    TapeHead0 += TD0.RepeaterCount[i] * TD0.Repeater[i].size() ;
    TapeHead1 += TD1.RepeaterCount[i] * TD1.Repeater[i].size() ;
    Overlap = std::min (TapeHead0, TapeHead1) - Overlap ;

    if (Overlap < (int)TD0.Repeater[i].size())
      TM_ERROR() ;
    }

  TapeHead0 += TD0.Wall[nPartitions].size() - 1 ;
  TapeHead1 += TD1.Wall[nPartitions].size() - 1 ;
  if (TapeHead0 != TD0.Rightmost || TapeHead1 != TD1.Rightmost) TM_ERROR() ;

  // Check the tape cells one by one
  TapePosition TP0, TP1 ;
  InitTapePosition (TD0, TP0) ;
  InitTapePosition (TD1, TP1) ;
  for (int i = TD0.Leftmost ; i <= TD0.Rightmost ; i++)
    if (NextCell (TD0, TP0, i) != NextCell (TD1, TP1, i))
      TM_ERROR() ;
  if (!TP0.Finished || !TP1.Finished) TM_ERROR() ;

  // Check that the tape heads match
  if (TP0.WallOffset == INT_MIN || TP1.WallOffset == INT_MIN) TM_ERROR() ;
  if (TP0.WallOffset + TD0.TapeHeadOffset != TP1.WallOffset + TD1.TapeHeadOffset)
    TM_ERROR() ;
  }

void Bouncer::InitTapePosition (const TapeDescriptor& TD, TapePosition& TP) const
  {
  TP.Repeat = TP.Offset = 0 ;
  TP.Partition = 0 ;
  TP.Finished = false ;
  TP.InWall = !TD.Wall[0].empty() ;
  TP.WallOffset = (TD.TapeHeadWall == 0) ? TD.Leftmost : INT_MIN ;
  }

uint8_t Bouncer::NextCell (const TapeDescriptor& TD, TapePosition& TP, int TapeHeadOffset) const
  {
  if (TP.Finished) TM_ERROR() ;

  uint8_t Cell ;
  if (TP.InWall)
    {
    Cell = TD.Wall[TP.Partition][TP.Offset] ;
    if (++TP.Offset == TD.Wall[TP.Partition].size())
      {
      if (TP.Partition == nPartitions) TP.Finished = true ;
      else
        {
        TP.Offset = TP.Repeat = 0 ;
        TP.InWall = false ;
        }
      }
    }
  else
    {
    Cell = TD.Repeater[TP.Partition][TP.Offset] ;
    if (++TP.Offset == TD.Repeater[TP.Partition].size())
      {
      TP.Offset = 0 ;
      if (++TP.Repeat == TD.RepeaterCount[TP.Partition])
        {
        TP.Repeat = 0 ;
        if (++TP.Partition > nPartitions) TM_ERROR() ;
        if (TP.Partition == TD.TapeHeadWall) TP.WallOffset = TapeHeadOffset + 1 ;
        if (TD.Wall[TP.Partition].empty())
          {
          if (TP.Partition == nPartitions) TP.Finished = true ;
          }
        else TP.InWall = true ;
        }
      }
    }
  return Cell ;
  }

Bouncer::TapeDescriptor& Bouncer::TapeDescriptor::operator= (const TapeDescriptor& TD)
  {
  B = TD.B ;
  State = TD.State ;
  Leftmost = TD.Leftmost ;
  Rightmost = TD.Rightmost ;
  TapeHeadWall = TD.TapeHeadWall ;
  TapeHeadOffset = TD.TapeHeadOffset ;

  for (uint32_t i = 0 ; ; i++)
    {
    Wall[i] = TD.Wall[i] ;

    if (i == B -> nPartitions) break ;

    Repeater[i] = TD.Repeater[i] ;
    RepeaterCount[i] = TD.RepeaterCount[i] ;
    }

  return *this ;
  }
