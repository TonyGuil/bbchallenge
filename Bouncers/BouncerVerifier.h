#pragma once

#include "Bouncer.h"

#define VERIFY_ERROR() \
  printf ("\n#%d: Error at line %d in %s (fpos 0x%lX)\n", \
  SeedDatabaseIndex, __LINE__, __FUNCTION__, ftell (fp)), exit (1)

class BouncerVerifier : public Bouncer
  {
public:
  BouncerVerifier (uint32_t SpaceLimit)
  : Bouncer (SpaceLimit, false)
    {
    }

  FILE* fp ;

  struct RunDescriptor
    {
    RunDescriptor (Bouncer* B) : TD0 (B), TD1 (B) { }
    uint32_t Partition ;
    Transition RepeaterTransition ;
    TapeDescriptor TD0 ;
    Transition WallTransition ;
    TapeDescriptor TD1 ;
    } ;

  void Verify (uint32_t SeedDatabaseIndex, const uint8_t* MachineSpec, FILE* fp) ;

  void ReadRunDescriptor (FILE* fp, RunDescriptor& RD) ;
  void ReadTransition (FILE* fp, Transition& Tr) ;
  void ReadSegment (FILE* fp, Segment& Seg) ;
  void ReadByteArray (FILE* fp, std::vector<uint8_t>& Data) ;
  void ReadTapeDescriptor (FILE* fp, TapeDescriptor& TD) ;

  uint32_t RepeaterCount[MAX_PARTITIONS] ;
  int TapeLeftmost, TapeRightmost ;
  } ;
