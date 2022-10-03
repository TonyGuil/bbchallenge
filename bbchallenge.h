// bbchallenge.h
//
// Header file for Busy Beaver Challenge code

#pragma once

//#define BIG_ENDIAN false

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define NSTATES        5
#define MAX_SPACE      12289
#define MACHINE_STATES 5
#define TAPE_SENTINEL  2

#define VERIF_HEADER_LENGTH 12 // Verification data header length
#define VERIF_ENTRY_LENGTH (VERIF_HEADER_LENGTH + VERIF_INFO_LENGTH)
  // VERIF_INFO_LENGTH must be defined in the cpp file

enum class DeciderTag : uint32_t
  {
  NONE                     = 1,
  CYCLERS                  = 1,
  TRANSLATED_CYCLERS_RIGHT = 2,
  TRANSLATED_CYCLERS_LEFT  = 3,
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


// inline uint32_t Read32 (FILE* fp)
//
// Read a 32-bit big-endian integer from a file, reversing the bytes if necessary

inline uint32_t Read32 (FILE* fp)
  {
  uint32_t t ;
  if (fread (&t, 4, 1, fp) != 1) printf ("\nRead error in Read32\n"), exit (1) ;
#if BIG_ENDIAN
  return t ;
#else
  return __builtin_bswap32 (t) ;
#endif
  }

// inline void Write32 (FILE* fp, uint32_t n)
//
// Write a big-endian 32-bit integer to a file, reversing the bytes if necessary

inline void Write32 (FILE* fp, uint32_t n)
  {
#if !BIG_ENDIAN
  n = __builtin_bswap32 (n) ;
#endif
  if (fwrite (&n, 4, 1, fp) != 1)
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
    }

  StepResult Step()
    {
    uint8_t Cell = Tape[TapeHead] ;
    if (Cell == TAPE_SENTINEL) return StepResult::OUT_OF_BOUNDS ;
    const StateDesc& S = TM[State][Cell] ;
    Tape[TapeHead] = S.Write ;
    if (S.Move) // Left
      {
      TapeHead-- ;
      if (TapeHead < Leftmost) Leftmost = TapeHead ;
      }
    else
      {
      TapeHead++ ;
      if (TapeHead > Rightmost) Rightmost = TapeHead ;
      }
    State = S.Next ;
    StepCount++ ;
    return State ? StepResult::OK : StepResult::HALT ;
    }

private:

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
    nSpaceLimited = Read32 (fp) ;
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
