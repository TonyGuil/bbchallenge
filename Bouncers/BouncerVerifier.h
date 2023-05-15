#pragma once

#include "Bouncer.h"

#define VERIFY_ERROR() \
  printf ("\n#%d: Error at line %d in %s (fpos 0x%lX)\n", \
  SeedDatabaseIndex, __LINE__, __FUNCTION__, ftell (fp)), exit (1)

class BouncerVerifier : public Bouncer
  {
public:
  BouncerVerifier (uint32_t MachineStates, uint32_t SpaceLimit)
  : Bouncer (MachineStates, SpaceLimit, false)
    {
    }

  struct RunDescriptor
    {
    uint32_t Partition ;
    SegmentTransition RepeaterTransition ;
    SegmentTransition WallTransition ;
    } ;

  void Verify (FILE* fp) ;
  void VerifyHalter (FILE* fp) ;

  void ReadRunDescriptor (FILE* fp, RunDescriptor& RD) ;
  void ReadTransition (FILE* fp, SegmentTransition& Tr) ;
  void ReadSegment (FILE* fp, Segment& Seg) ;
  void ReadByteArray (FILE* fp, ustring& Data) ;
  void ReadTapeDescriptor (FILE* fp, TapeDescriptor& TD) ;

  void CheckWallTransition (TapeDescriptor& TD, const SegmentTransition& Tr, bool Final) ;
  void CheckRepeaterTransition (uint32_t Partition,TapeDescriptor& TD,
    const SegmentTransition& Tr) ;

  virtual bool Verifying() const override { return true ; }

  uint32_t RepeaterCount[MAX_PARTITIONS] ;
  int FinalLeftmost, FinalRightmost ;
  } ;
