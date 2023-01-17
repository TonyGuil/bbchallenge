// bbchallenge.h
//
// Header file for Busy Beaver Challenge code

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boost/endian/conversion.hpp>

#define NSTATES        5
#define MAX_SPACE      12289
#define MAX_TIME       47176870 
#define MACHINE_STATES 5
#define TAPE_SENTINEL  2

// Number of machines of each type in the Seed Database:
#define NTIME_LIMITED  14322029
#define NSPACE_LIMITED 74342035

#define VERIF_HEADER_LENGTH 12 // Verification data header length

#define TM_ERROR() \
  printf ("\n#%d: Error at line %d in %s\n", \
  SeedDatabaseIndex, __LINE__, __FUNCTION__), exit (1)

// For constant-length verification data, define VERIF_INFO_LENGTH
// in the cpp file, and then you can use VERIF_ENTRY_LENGTH:
#define VERIF_ENTRY_LENGTH (VERIF_HEADER_LENGTH + (VERIF_INFO_LENGTH))

enum class DeciderTag : uint32_t
  {
  NONE                    = 0,
  CYCLER                  = 1,
  TRANSLATED_CYCLER_RIGHT = 2,
  TRANSLATED_CYCLER_LEFT  = 3,
  BACKWARD_REASONING      = 4,
  HALTING_SEGMENT         = 5,
  BOUNCER                 = 6,

  // Finite Automata Reduction variants
  FAR_DFA_ONLY            = 10,
  FAR_DFA_NFA             = 11,
  } ;

enum class StepResult : uint8_t
  {
  OK,
  HALT,
  OUT_OF_BOUNDS
  } ;

struct StateDesc
  {
  uint8_t Write ;
  uint8_t Move ; // 1 = Left, 0 = Right
  uint8_t Next ;
  } ;

//
// Endianness (data files are big-endian, platform may be big- or little-endian)
//

inline uint32_t Load32 (const void* p)
  {
  return boost::endian::big_to_native<uint32_t> (*(uint32_t*)p) ;
  }

inline uint16_t Load16 (const void* p)
  {
  return boost::endian::big_to_native<uint16_t> (*(uint16_t*)p) ;
  }

inline uint32_t Save32 (void* p, uint32_t n)
  {
  *(uint32_t*)p = boost::endian::native_to_big<uint32_t> (n) ;
  return 4 ;
  }

inline uint32_t Save16 (void* p, uint16_t n)
  {
  *(uint16_t*)p = boost::endian::native_to_big<uint16_t> (n) ;
  return 2 ;
  }

// inline uint32_t Read32 (FILE* fp)
// inline uint32_t Read16u (FILE* fp)
// inline int32_t Read16s (FILE* fp)
// inline uint32_t Read8u (FILE* fp)
// inline int32_t Read8s (FILE* fp)
//
// Read signed or unsigned big-endian integers of various sizes from a file,
// reversing the bytes if necessary

inline uint32_t Read32 (FILE* fp)
  {
  uint32_t t ;
  if (fread (&t, 4, 1, fp) != 1) printf ("\nRead error in Read32\n"), exit (1) ;
  return boost::endian::big_to_native<uint32_t> (t) ;
  }

inline uint32_t Read16u (FILE* fp)
  {
  uint16_t t ;
  if (fread (&t, 2, 1, fp) != 1) printf ("\nRead error in Read16u\n"), exit (1) ;
  return boost::endian::big_to_native<uint16_t> (t) ;
  }

inline int32_t Read16s (FILE* fp)
  {
  int16_t t ;
  if (fread (&t, 2, 1, fp) != 1) printf ("\nRead error in Read16s\n"), exit (1) ;
  return boost::endian::big_to_native<int16_t> (t) ;
  }

inline uint32_t Read8u (FILE* fp)
  {
  uint8_t t ;
  if (fread (&t, 1, 1, fp) != 1) printf ("\nRead error in Read8u\n"), exit (1) ;
  return t ;
  }

inline int32_t Read8s (FILE* fp)
  {
  int8_t t ;
  if (fread (&t, 1, 1, fp) != 1) printf ("\nRead error in Read8s\n"), exit (1) ;
  return t ;
  }

// inline void Write32 (FILE* fp, uint32_t n)
// inline void Write16 (FILE* fp, uint32_t n)
// inline void Write8 (FILE* fp, uint32_t n)
//
// Write a big-endian integer to a file, reversing the bytes if necessary

inline void Write32 (FILE* fp, uint32_t n)
  {
  boost::endian::native_to_big_inplace (n) ;
  if (fp && fwrite (&n, 4, 1, fp) != 1)
    printf ("Write error\n"), exit (1) ;
  }

inline void Write16 (FILE* fp, uint16_t n)
  {
  boost::endian::native_to_big_inplace (n) ;
  if (fp && fwrite (&n, 2, 1, fp) != 1)
    printf ("Write error\n"), exit (1) ;
  }

inline void Write8 (FILE* fp, uint8_t n)
  {
  if (fp && fwrite (&n, 1, 1, fp) != 1)
    printf ("Write error\n"), exit (1) ;
  }

class TuringMachine
  {
public:
  TuringMachine (uint32_t TimeLimit, uint32_t SpaceLimit)
  : TimeLimit (TimeLimit)
  , SpaceLimit (SpaceLimit)
    {
    memset (TM[0], 0, 2 * sizeof (StateDesc)) ;
    TapeWorkspace = new uint8_t[2 * SpaceLimit + 1] ;
    TapeWorkspace[0] = TapeWorkspace[2 * SpaceLimit] = TAPE_SENTINEL ;
    Tape = TapeWorkspace + SpaceLimit ;
    }

  ~TuringMachine()
    {
    delete[] TapeWorkspace ;
    }

  uint32_t SeedDatabaseIndex ;

  uint8_t* Tape ;
  int TapeHead ;
  uint8_t State ;

  uint32_t TimeLimit ;
  uint32_t SpaceLimit ;
  int Leftmost ;
  int Rightmost ;
  uint32_t StepCount ;
  int8_t RecordBroken ; // +/-1 if the latest step broke a right or left record

  void Initialise (int Index, const uint8_t* MachineSpec)
    {
    SeedDatabaseIndex = Index ;
    StateDesc* S = &TM[1][0] ;
    for (int i = 0 ; i < 2 * NSTATES ; i++, S++)
      {
      S -> Write = *MachineSpec++ ;
      S -> Move = *MachineSpec++ ;
      S -> Next = *MachineSpec++ ;
      }

    memset (Tape - SpaceLimit + 1, 0, 2 * SpaceLimit - 1) ;
    TapeHead = Leftmost = Rightmost = 0 ;
    State = 1 ;
    StepCount = 0 ;
    RecordBroken = 0 ;
    }

  TuringMachine& operator= (const TuringMachine& Src)
    {
    if (TimeLimit != Src.TimeLimit || SpaceLimit != Src.SpaceLimit)
      printf ("Error 1 in TuringMachine::operator=\n"), exit (1) ;

    SeedDatabaseIndex = Src.SeedDatabaseIndex ;
    TapeHead = Src.TapeHead ;
    State = Src.State ;
    Leftmost = Src.Leftmost ;
    Rightmost = Src.Rightmost ;
    StepCount = Src.StepCount ;
    RecordBroken = Src.RecordBroken ;

    memcpy (TM, Src.TM, sizeof (TM)) ;
    memcpy (TapeWorkspace, Src.TapeWorkspace, 2 * SpaceLimit + 1) ;

    return *this ;
    }

  StepResult Step()
    {
    RecordBroken = 0 ;
    uint8_t Cell = Tape[TapeHead] ;
    if (Cell == TAPE_SENTINEL) return StepResult::OUT_OF_BOUNDS ;
    const StateDesc& S = TM[State][Cell] ;
    Tape[TapeHead] = S.Write ;
    if (S.Move) // Left
      {
      TapeHead-- ;
      if (TapeHead < Leftmost)
        {
        Leftmost = TapeHead ;
        RecordBroken = -1 ;
        }
      }
    else
      {
      TapeHead++ ;
      if (TapeHead > Rightmost)
        {
        Rightmost = TapeHead ;
        RecordBroken = 1 ;
        }
      }
    State = S.Next ;
    StepCount++ ;
    return State ? StepResult::OK : StepResult::HALT ;
    }

protected:

  StateDesc TM[NSTATES + 1][2] ;
  uint8_t* TapeWorkspace ;
  } ;

// class TuringMachineReader
//
// Class for reading a 30-byte machine spec from the seed database file
//
// If no filename is specified, we assume we are running from a sub-directory
// of bbchallenge, so the filename defaults to ../SeedDatabase.bin.

#define MACHINE_SPEC_SIZE 30

class TuringMachineReader
  {
public:
  TuringMachineReader (const char* Filename = 0)
    {
    if (Filename == 0 || Filename[0] == 0) Filename = "../SeedDatabase.bin" ;
    fp = fopen (Filename, "rb") ;
    if (fp == 0) printf ("Can't open file \"%s\"\n", Filename), exit (1) ;
    nTimeLimited = Read32 (fp) ;
    if (nTimeLimited != NTIME_LIMITED)
      printf ("nTimeLimited discrepancy!\n"), exit (1) ;
    nSpaceLimited = Read32 (fp) ;
    if (nSpaceLimited != NSPACE_LIMITED)
      printf ("nSpaceLimited discrepancy!\n"), exit (1) ;
    nMachines = Read32 (fp) ;
    if (nMachines != nTimeLimited + nSpaceLimited)
      printf ("Invalid seed database file\n"), exit (1) ;
    }

  ~TuringMachineReader()
    {
    fclose (fp) ;
    }

  void Read (uint32_t SeedDatabaseIndex, uint8_t* MachineSpec, uint32_t n = 1)
    {
    if (SeedDatabaseIndex + n > nMachines)
      printf ("Invalid machine index %d\n", SeedDatabaseIndex + n - 1), exit (1) ;

    // File offset can exceed 2^31, so we need to use fseek064:
    if (fseeko64 (fp, uint32_t(MACHINE_SPEC_SIZE * (SeedDatabaseIndex + 1)), SEEK_SET))
      printf ("Seek error for machine %d\n", SeedDatabaseIndex), exit (1) ;

    if (fread (MachineSpec, 30, n, fp) != n)
      printf ("Read error for machine %d\n", SeedDatabaseIndex), exit (1) ;
    }

  uint32_t nMachines ;
  uint32_t nTimeLimited ;
  uint32_t nSpaceLimited ;

private:
  FILE* fp ;
  } ;
