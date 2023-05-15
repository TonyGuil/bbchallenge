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
    // By the time a transition gets as far as the verification file, it must be non-empty:
    if (Verifying()) TM_ERROR() ;

    // But the Decider can create temporary empty transitions:
    return ;
    }

  // Initial tape head must be within the tape boundaries; final tape head
  // can be +/-1 either side
  if (Tr.nSteps != 0 && (Tr.Initial.TapeHead < 0 || Tr.Initial.TapeHead >= (int)Tr.Initial.Tape.size()))
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
// bool Bouncer::TestFollowOn (const Segment& Seg1, const Segment& Seg2)
//
// Check that Seg2 follows on from Seg1:
//  - Seg2.State = Seg1.State;
//  - after aligning the tapes so that Tr1.Final.TapeHead and Tr2.Initial.TapeHead
//    are equal, the tapes agree with each other on the overlapping segment.
//
// If the checks fail, CheckFollowOn aborts with TM_ERROR but TestFollowOn just returns false.

void Bouncer::CheckFollowOn (const Segment& Seg1, const Segment& Seg2)
  {
  if (!TestFollowOn (Seg1, Seg2)) TM_ERROR() ;
  }

bool Bouncer::TestFollowOn (const Segment& Seg1, const Segment& Seg2)
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
    if ((Seg1.Tape[i] ^ Seg2.Tape[i - Shift]) == 1)
      return false ;

  return true ;
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

// void Bouncer::TestTapesEquivalent (const TapeDescriptor& TD0, const TapeDescriptor& TD1)
//
// Test whether two TapeDescriptors define the same tape
//
// In fact it checks that the two TapeDescriptors will still define the same
// tape if we increase their RepeaterCounts by the same amount in each. This
// is necessary for the Verifier's inductive proof to go through.

bool Bouncer::TestTapesEquivalent (const TapeDescriptor& TD0, const TapeDescriptor& TD1) const
  {
  if (TD0.State != TD1.State) return false ;
  if (TD0.TapeHeadWall != TD1.TapeHeadWall) return false ;

  if (TD0.Leftmost != TD1.Leftmost) return false ;
  if (TD0.Rightmost != TD1.Rightmost) return false ;

  // Check that the repeater parameters match
  for (uint32_t i = 0 ; i < nPartitions ; i++)
    {
    if (TD0.RepeaterCount[i] != TD1.RepeaterCount[i]) return false ;
    if (TD0.Repeater[i].size() != TD1.Repeater[i].size()) return false ;
    if (TD0.RepeaterCount[i] < 1 || TD0.Repeater[i].size() == 0) return false ;
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
      return false ;
    }

  TapeHead0 += TD0.Wall[nPartitions].size() - 1 ;
  TapeHead1 += TD1.Wall[nPartitions].size() - 1 ;
  if (TapeHead0 != TD0.Rightmost || TapeHead1 != TD1.Rightmost) return false ;

  // Check the tape cells one by one
  TapePosition TP0, TP1 ;
  InitTapePosition (TD0, TP0) ;
  InitTapePosition (TD1, TP1) ;
  for (int i = TD0.Leftmost ; i <= TD0.Rightmost ; i++)
    if (NextCell (TD0, TP0, i) != NextCell (TD1, TP1, i))
      return false ;
  if (!TP0.Finished || !TP1.Finished) return false ;

  // Check that the tape heads match
  if (TP0.WallOffset == INT_MIN || TP1.WallOffset == INT_MIN) return false ;
  if (TP0.WallOffset + TD0.TapeHeadOffset != TP1.WallOffset + TD1.TapeHeadOffset)
    return false ;

  return true ;
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
