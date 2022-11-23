// The Bouncer class contains functions and data that are used by both
// the Decider and the Verifier

#include "Bouncer.h"

void Bouncer::CheckTape (const TuringMachine* TM, const TapeDescriptor& TD)
  {
  if (TD.State != TM -> State) TM_ERROR() ;
  if (TD.TapeHeadWall > nPartitions) TM_ERROR() ;

  int TapeHead = TD.Leftmost ;
  for (uint32_t i = 0 ; ; i++)
    {
    if (i == TD.TapeHeadWall && TM -> TapeHead != TapeHead + TD.TapeHeadOffset)
      TM_ERROR() ;

    for (uint32_t j = 0 ; j < TD.Wall[i].size() ; j++)
      if (TM -> Tape[TapeHead++] != TD.Wall[i][j])
        TM_ERROR() ;

    if (i == nPartitions) break ;

    for (uint32_t j = 0 ; j < TD.RepeaterCount[i] ; j++)
      for (uint32_t k = 0 ; k < TD.Repeater[i].size() ; k++)
        if (TM -> Tape[TapeHead++] != TD.Repeater[i][k])
          TM_ERROR() ;
    }
  if (TapeHead != TD.Rightmost + 1) TM_ERROR() ;
  }

void Bouncer::CheckTransition (const Transition& Tr) const
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
    const StateDesc& S = TM[State][Cell] ;
    Tape[TapeHead] = S.Write ;
    if (S.Move) TapeHead-- ;
    else TapeHead++ ;
    State = S.Next ;
    if (State < 1 || State > 5) TM_ERROR() ;
    }

  if (State != Tr.Final.State) TM_ERROR() ;
  if (TapeHead != Tr.Final.TapeHead) TM_ERROR() ;
  if (Tape != Tr.Final.Tape) TM_ERROR() ;
  }

bool Bouncer::MatchSegments (const Segment& Seg1, const Segment& Seg2)
  {
  if (Seg1.State != Seg2.State) return false ;

  if (Seg1.Tape.empty() || Seg2.Tape.empty()) return true ;

  int Shift = Seg1.TapeHead - Seg2.TapeHead ;
  int Left = 0 ;
  int Right = Seg1.Tape.size() ;
  if (Shift > 0)
    {
    Left = Shift ;
    if (Left > (int)Seg1.Tape.size()) return false ;
    if (Right > int (Seg2.Tape.size() + Shift))
      Right = Seg2.Tape.size() + Shift ;
    }
  else
    {
    Right = Seg2.Tape.size() + Shift ;
    if (Right > (int)Seg1.Tape.size())
      Right = Seg1.Tape.size() ;
    }
  if (Left > Right) return false ;
  for (int i = Left ; i < Right ; i++)
    if ((Seg1.Tape.at(i) ^ Seg2.Tape.at(i - Shift)) == 1)
      return false ;

  return true ;
  }

// void Bouncer::CheckWallTransition (TapeDescriptor TD0,
//   TapeDescriptor TD1, const Transition& Tr)
//
// Check that Tr transforms tape TD0 into tape TD1
//
// Note that TD0 and TD1 are passed by value, not by reference; so we
// are free to modify them here

void Bouncer::CheckWallTransition (TapeDescriptor TD0,
  TapeDescriptor TD1, const Transition& Tr)
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

  // Check that the Transition's initial configuration is compatible with the TD0 tape
  CheckSegment (TD0, Tr.Initial, Wall) ;

  // Check that the Tr.Final is compatible with the TD1 tape
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

  // Insert the Transition's final tape into TD0
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
//   const TapeDescriptor& TD1, const Transition& Tr)
//
// Check that Tr transforms tape TD0 into tape TD1

void Bouncer::CheckRepeaterTransition (const TapeDescriptor& TD0,
  const TapeDescriptor& TD1, const Transition& Tr)
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
  if (Len0 != Len1) TM_ERROR() ;

  // Check that the Transition's initial configuration is compatible with the TD0 tape
  CheckSegment (TD0, Tr.Initial, TD0.TapeHeadWall) ;

  // Check that the Transition's final configuration is compatible with the TD1 tape
  CheckSegment (TD1, Tr.Final, TD1.TapeHeadWall) ;

  // Check the inductive step
  int Shift = Tr.Final.TapeHead - Tr.Initial.TapeHead ;
  if (Shift == 0) TM_ERROR() ;
  if (Shift < 0) CheckLeftwardRepeater (TD0, TD1, Tr) ;
  else CheckRightwardRepeater (TD0, TD1, Tr) ;
  }

// void Bouncer::CheckLeftwardRepeater (TapeDescriptor TD0, TapeDescriptor TD1, const Transition& Tr)
//
// Note that TD0 and TD1 are passed by value, not by reference; so we
// are free to modify them here

void Bouncer::CheckLeftwardRepeater (TapeDescriptor TD0, TapeDescriptor TD1, const Transition& Tr)
  {
  TD0.State = Tr.Final.State ;

  uint32_t Wall = TD0.TapeHeadWall ;
  uint32_t Stride = TD0.Repeater[Wall - 1].size() ;
  if ((int)Stride != Tr.Initial.TapeHead - Tr.Final.TapeHead) TM_ERROR() ;

  // Expand the current wall to the right if necessary, to ensure that
  // Tr.Initial doesn't encroach on the repeaters beyond
  int t = Tr.Initial.Tape.size() - Tr.Initial.TapeHead ;
  t -= TD0.Wall[Wall].size() - TD0.TapeHeadOffset ;
  ExpandWallsRightward (TD0, TD1, Wall, t) ;

  // Expand the destination wall to the left if necessary, to ensure that
  // Tr.Final doesn't encroach on the repeaters beyond
  t = Tr.Initial.TapeHead - TD0.TapeHeadOffset ;
  t -= TD0.Wall[Wall - 1].size() ;
  t -= Stride ;
  ExpandWallsLeftward (TD0, TD1, Wall - 1, t) ;

  // Check that the Tr.Initial is compatible with the TD0 tape
  CheckSegment (TD0, Tr.Initial, TD0.TapeHeadWall) ;

  // How much does Tr.Initial.Tape overhang the Wall into the array of Repeaters?
  int Overhang = Tr.Initial.TapeHead - TD0.TapeHeadOffset ;
  if (Overhang < 0) TM_ERROR() ;

  // Check that there's no gap between the wall and Tr.Initial.Tape
  if (Tr.Initial.TapeHead > int(Tr.Initial.Tape.size() + TD0.TapeHeadOffset))
    TM_ERROR() ;

  // Check that Wall matches Tr.Initial.Tape where they overlap
  for (uint32_t i = 0 ; i < Tr.Initial.Tape.size() - Overhang ; i++)
    if (TD0.Wall[Wall].at(i) != Tr.Initial.Tape.at(i + Overhang)) TM_ERROR() ;

  // Check that the rest of Tr.Initial.Tape consists of appropriately aligned
  // copies of the Repeater
  int Rotate = Overhang % Stride ; Rotate = Stride - Rotate ; Rotate %= Stride ;
  for (int i = 0 ; i < Overhang ; i++)
    if (Tr.Initial.Tape.at(i) != TD0.Repeater[Wall - 1].at((i + Rotate) % Stride))
      TM_ERROR() ;

  // Check that the destination wall ends with the same copies
  for (int i = 0 ; i < int(Overhang - Stride) ; i++)
    if (Tr.Initial.Tape.at(i) != TD0.Wall[Wall - 1].at(i + TD0.Wall[Wall - 1].size() - Overhang + Stride))
      TM_ERROR() ;

  // Append the final segment of Tr.Initial.Tape to the destination wall
  TD0.TapeHeadOffset = TD0.Wall[Wall - 1].size() + Tr.Initial.TapeHead - Overhang ;
  TD0.Wall[Wall - 1].insert (TD0.Wall[Wall - 1].end(),
    Tr.Initial.Tape.begin() + Overhang, Tr.Initial.Tape.end()) ;

  // Truncate the wall so it ends to the right of Tr.Initial.Tape
  TD0.Wall[Wall].erase (TD0.Wall[Wall].begin(),
    TD0.Wall[Wall].begin() + Tr.Initial.Tape.size() - Overhang) ;

  // Replace the Repeater with the final segment of Tr.Final.Tape
  for (uint32_t i = 0 ; i < Stride ; i++)
    TD0.Repeater[Wall - 1].at(i) = Tr.Final.Tape.at(Tr.Final.Tape.size() - Stride + i) ;
  TD0.TapeHeadWall = Wall - 1 ;

  // Now TD0 and TD1 should match
  CheckTapesEquivalent (TD0, TD1) ;
  }

// void Bouncer::CheckRightwardRepeater (TapeDescriptor TD0, TapeDescriptor TD1, const Transition& Tr)
//
// Note that TD0 and TD1 are passed by value, not by reference; so we
// are free to modify them here

void Bouncer::CheckRightwardRepeater (TapeDescriptor TD0, TapeDescriptor TD1, const Transition& Tr)
  {
  TD0.State = Tr.Final.State ;

  uint32_t Wall = TD0.TapeHeadWall ;
  uint32_t Stride = TD0.Repeater[Wall].size() ;
  if ((int)Stride != Tr.Final.TapeHead - Tr.Initial.TapeHead) TM_ERROR() ;

  // Expand the current wall to the left if necessary, to ensure that
  // Tr.Initial doesn't encroach on the repeaters beyond
  int t = Tr.Initial.TapeHead - TD0.TapeHeadOffset ;
  ExpandWallsLeftward (TD0, TD1, Wall, t) ;

  // Expand the destination wall to the right if necessary, to ensure that
  // Tr.Final doesn't encroach on the repeaters beyond
  t = Tr.Initial.Tape.size() - Tr.Initial.TapeHead ;
  t -= TD0.Wall[Wall].size() - TD0.TapeHeadOffset ;
  t -= TD0.Wall[Wall + 1].size() ;
  t -= Stride ;
  ExpandWallsRightward (TD0, TD1, Wall + 1, t) ;

  // Check that the Tr.Initial is compatible with the TD0 tape
  CheckSegment (TD0, Tr.Initial, TD0.TapeHeadWall) ;

  // InitOffset is the offset of Tr.Initial.Tape[0] in the current wall
  int InitOffset = TD0.TapeHeadOffset - Tr.Initial.TapeHead ;
  if (InitOffset < 0)
    TM_ERROR() ; // Should never happen

  // How much does Tr.Initial.Tape overhang the Wall into the array of Repeaters?
  int Overhang = InitOffset + Tr.Initial.Tape.size() - TD0.Wall[Wall].size() ;

  if (Overhang < 0) TM_ERROR() ;

  // Check that there's no gap between the wall and Tr.Initial.Tape
  if (TD0.TapeHeadOffset > int(TD0.Wall[Wall].size() + Tr.Initial.TapeHead))
    TM_ERROR() ;

  // Check that Wall matches Tr.Initial.Tape where they overlap
  for (uint32_t i = InitOffset ; i < TD0.Wall[Wall].size() ; i++)
    if (TD0.Wall[Wall].at(i) != Tr.Initial.Tape.at(i - InitOffset)) TM_ERROR() ;

  // Check that the rest of Tr.Initial.Tape consists of appropriately aligned
  // copies of the Repeater
  for (uint32_t i = TD0.Wall[Wall].size() - InitOffset ; i < Tr.Initial.Tape.size() ; i++)
    if (Tr.Initial.Tape.at(i) != TD0.Repeater[Wall].at((i - (TD0.Wall[Wall].size() - InitOffset)) % Stride))
      TM_ERROR() ;

  // Check that the destination wall starts with the same copies
  for (uint32_t i = TD0.Wall[Wall].size() - InitOffset + Stride ; i < Tr.Initial.Tape.size() ; i++)
    if (Tr.Initial.Tape.at(i) != TD0.Wall[Wall + 1].at(i - (TD0.Wall[Wall].size() - InitOffset + Stride)))
      TM_ERROR() ;

  // Prepend the initial segment of Tr.Initial.Tape to the destination wall
  TD0.Wall[Wall + 1].insert (TD0.Wall[Wall + 1].begin(), Tr.Initial.Tape.begin(),
    Tr.Initial.Tape.begin() + TD0.Wall[Wall].size() - InitOffset) ;

  // Truncate the wall so it ends to the left of Tr.Initial.Tape
  TD0.Wall[Wall].resize (InitOffset) ;

  // Replace the Repeater with the initial segment of Tr.Final.Tape
  for (uint32_t i = 0 ; i < Stride ; i++)
    TD0.Repeater[Wall].at(i) = Tr.Final.Tape.at(i) ;

  TD0.TapeHeadOffset = Tr.Initial.TapeHead ;
  TD0.TapeHeadWall = Wall + 1 ;

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

  // We expect the resulting RepeaterCount to be at least 3 (not strictly
  // necessary, but it covers all known cases and avoids unpleasant shocks)
  if ((int)TD0.RepeaterCount[Wall - 1] < DeltaCount + 3) TM_ERROR() ;

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

  // We expect the resulting RepeaterCount to be at least 3 (not strictly
  // necessary, but it covers all known cases and avoids unpleasant shocks)
  if ((int)TD0.RepeaterCount[Wall] < DeltaCount + 3) TM_ERROR() ;

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

void Bouncer::CheckTapesEquivalent (const TapeDescriptor& TD0, const TapeDescriptor& TD1)
  {
  if (TD0.State != TD1.State) TM_ERROR() ;
  if (TD0.TapeHeadWall != TD1.TapeHeadWall) TM_ERROR() ;

  int Slippage = 0 ;
  for (uint32_t i = 0 ; ; i++)
    {
    if (i == TD0.TapeHeadWall && TD0.TapeHeadOffset != TD1.TapeHeadOffset + Slippage)
      TM_ERROR() ;

    // For each tape, build the segment covered by the union of the two walls.
    // UnionLeft and UnionRight are relative to TD0.Wall[i][0]:
    int UnionLeft = std::min (0, Slippage) ;
    int UnionRight = TD0.Wall[i].size() ;
    UnionRight = std::max (UnionRight, (int)TD1.Wall[i].size() + Slippage) ;
    uint32_t RepeaterSize ;

    // Build Segment0, in three parts
    std::vector<uint8_t> Segment0 (UnionRight - UnionLeft) ;
    if (i != 0)
      {
      // Repeaters to the left
      RepeaterSize = TD0.Repeater[i - 1].size() ;
      for (int j = 0 ; j < -UnionLeft ; j++)
        {
        int t = -UnionLeft % RepeaterSize ;
        t = j + RepeaterSize - t ;
        t %= RepeaterSize ;
        Segment0.at(j) = TD0.Repeater[i - 1].at(t) ;
        }
      }

    // Wall
    for (int j = 0 ; j < (int)TD0.Wall[i].size() ; j++)
      Segment0.at(j - UnionLeft) = TD0.Wall[i].at(j) ;

    if (i != nPartitions)
      {
      // Repeaters to the right
      RepeaterSize = TD0.Repeater[i].size() ;
      for (int j = TD0.Wall[i].size() ; j < UnionRight ; j++)
        Segment0.at(j - UnionLeft) = TD0.Repeater[i].at((j - TD0.Wall[i].size()) % RepeaterSize) ;
      }

    // make UnionLeft and UnionRight relative to TD1.Wall[i][0]:
    UnionLeft -= Slippage ;
    UnionRight -= Slippage ;

    // Build Segment1, in three parts
    std::vector<uint8_t> Segment1 (UnionRight - UnionLeft) ;
    if (i != 0)
      {
      // Repeaters to the left
      RepeaterSize = TD1.Repeater[i - 1].size() ;
      for (int j = 0 ; j < -UnionLeft ; j++)
        {
        int t = -UnionLeft % RepeaterSize ;
        t = j + RepeaterSize - t ;
        t %= RepeaterSize ;
        Segment1.at(j) = TD1.Repeater[i - 1].at(t) ;
        }
      }

    // Wall
    for (int j = 0 ; j < (int)TD1.Wall[i].size() ; j++)
      Segment1.at(j - UnionLeft) = TD1.Wall[i].at(j) ;

    if (i != nPartitions)
      {
      // Repeaters to the right
      RepeaterSize = TD1.Repeater[i].size() ;
      for (int j = TD1.Wall[i].size() ; j < UnionRight ; j++)
        Segment1.at(j - UnionLeft) = TD1.Repeater[i].at((j - TD1.Wall[i].size()) % RepeaterSize) ;
      }

    // Check that they are equal
    if (Segment0 != Segment1)
      TM_ERROR() ;

    Slippage += TD1.Wall[i].size() - TD0.Wall[i].size() ;

    if (i == nPartitions) break ;

    if (TD0.RepeaterCount[i] != TD1.RepeaterCount[i]) TM_ERROR() ;

    // TD1.Repeater[i] should be a rotation of TD0.Repeater[i]
    RepeaterSize = TD0.Repeater[i].size() ;
    if (TD1.Repeater[i].size() != RepeaterSize) TM_ERROR() ;
    if (Slippage >= 0)
      {
      for (uint32_t j = 0 ; j < RepeaterSize ; j++)
        if (TD1.Repeater[i][j] != TD0.Repeater[i][(j + Slippage) % RepeaterSize])
          TM_ERROR() ;
      }
    else
      {
      for (uint32_t j = 0 ; j < RepeaterSize ; j++)
        if (TD1.Repeater[i][(j - Slippage) % RepeaterSize] != TD0.Repeater[i][j])
          TM_ERROR() ;
      }
    }
  if (Slippage != 0) TM_ERROR() ;
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
