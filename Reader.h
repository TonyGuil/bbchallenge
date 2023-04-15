// Reader.h
//
// TuringMachineReader class

#pragma once

// class TuringMachineReader
//
// Class for reading a machine spec from a machine database file
//
// Constructor:
//
//   TuringMachineReader (uint32_t MachineStates, const char* Filename = 0, bool Binary = true)
//
// If no filename is specified, we assume we are running from a sub-directory
// of bbchallenge, so the filename defaults to ../SeedDatabase.bin. This requires
// MachineStates = 5.
//
// The file can contain binary or text records; if text, the line separator must be
// a single character (not CR/LF).
//
// If MachineStates = 5 and Binary is true, we expect a 30-byte header starting with
// (nTimeLimited, nSpaceLimited, nMachines).

#include "bbchallenge.h"

#define MAX_MACHINE_SPEC_SIZE (MAX_MACHINE_STATES * (MAX_MACHINE_STATES + 1))

class CommonParams ;
class DeciderParams ;

class TuringMachineReader
  {
public:
  TuringMachineReader() { }
  TuringMachineReader (const CommonParams* Params) ;
  TuringMachineReader (const DeciderParams* Params) ;

  void SetParams (const CommonParams* Params) ;
  void SetParams (const DeciderParams* Params) ;

  void Read (uint32_t MachineIndex, uint8_t* MachineSpec, uint32_t n = 1) ;
  uint32_t Next (uint8_t* MachineSpec) ;

  const CommonParams* Params ;

  bool OrigSeedDatabase ; // Original SeedDatabase.bin file
  uint32_t nMachines ;
  uint32_t MachineSpecSize ;
  bool SingleEntry ;

  // For binary 5-state SeedDatabaseIndex only:
  uint32_t nTimeLimited ;
  uint32_t nSpaceLimited ;

protected:
  void ConvertToBinary (uint8_t* BinSpec, const uint8_t* TextSpec) ;
  void CheckMachineSpec (uint8_t* MachineSpec) ;

  FILE* fpDatabase ;
  FILE* fpInput ;
  uint32_t MachinesInDatabase ;
  uint32_t MachinesRead ;
  uint32_t SpecSizeInFile ;
  } ;
