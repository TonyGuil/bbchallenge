#include "Params.h"

TuringMachineReader::TuringMachineReader (const CommonParams* Params)
  {
  SetParams (Params) ;
  }

TuringMachineReader::TuringMachineReader (const DeciderParams* Params)
  {
  SetParams (Params) ;
  }

void TuringMachineReader::SetParams (const CommonParams* Params)
  {
  this -> Params = Params ;

  SingleEntry = false ;

  if (Params -> MachineStates > MAX_MACHINE_STATES)
    printf ("Max machine states (6) exceeded\n"), exit (1) ;

  fpDatabase = Params -> fpDatabase ;
  fpInput = Params -> fpInput ;
  if (fpDatabase == 0 && Params -> MachineSpec.empty())
    {
    if (Params -> MachineStates != 5 || !Params -> BinaryMachineSpecs)
      printf ("Invalid parameters for SeedDatabase.bin\n"), exit (1) ;
    fpDatabase = fopen ("../SeedDatabase.bin", "rb") ;
    if (fpDatabase == 0) printf ("Can't open ../SeedDatabase/bin\n"), exit (1) ;
    }

  OrigSeedDatabase = (Params -> MachineStates == 5 && Params -> BinaryMachineSpecs) ;
  MachineSpecSize = SpecSizeInFile = 6 * Params -> MachineStates ;
  if (!Params -> BinaryMachineSpecs)
    SpecSizeInFile += Params -> MachineStates ; // Allow for underscores and trailing newline

  if (!Params -> MachineSpec.empty())
    MachinesInDatabase = 0 ;
  else if (OrigSeedDatabase)
    {
    nTimeLimited = Read32 (fpDatabase) ;
    if (nTimeLimited != NTIME_LIMITED)
      printf ("nTimeLimited discrepancy!\n"), exit (1) ;
    nSpaceLimited = Read32 (fpDatabase) ;
    if (nSpaceLimited != NSPACE_LIMITED)
      printf ("nSpaceLimited discrepancy!\n"), exit (1) ;
    MachinesInDatabase = Read32 (fpDatabase) ;
    if (MachinesInDatabase != nTimeLimited + nSpaceLimited)
      printf ("Invalid seed database file\n"), exit (1) ;
    }
  else
    {
    nTimeLimited = nSpaceLimited = 0 ; // Unknown

    if (fseek (fpDatabase, 0, SEEK_END)) printf ("fseek failed\n"), exit (1) ;
    uint32_t InputFileSize = ftell (fpDatabase) ;
    if (fseek (fpDatabase, 0, SEEK_SET)) printf ("fseek failed\n"), exit (1) ;

    if (InputFileSize % SpecSizeInFile == SpecSizeInFile - 1)
      InputFileSize++ ; // Allow for missing newline at end of file
    if (InputFileSize % SpecSizeInFile != 0)
      printf ("Invalid machine spec file size\n"), exit (1) ;
    MachinesInDatabase = InputFileSize / SpecSizeInFile ;
    }

  if (Params -> Verifying()) nMachines = Read32 (Params -> fpVerify) ;

  MachinesRead = 0 ;
  }

void TuringMachineReader::SetParams (const DeciderParams* Params)
  {
  SetParams (static_cast<const CommonParams*> (Params)) ;

  if (Params -> TestMachinePresent || !Params -> MachineSpec.empty())
    {
    SingleEntry = true ;
    nMachines = 1 ;
    }
  else if (fpInput)
    {
    if (fseek (fpInput, 0, SEEK_END)) printf ("fseek failed\n"), exit (1) ;
    uint32_t InputFileSize = ftell (fpInput) ;
    if (fseek (fpInput, 0, SEEK_SET)) printf ("fseek failed\n"), exit (1) ;
    if (InputFileSize & 3) printf ("Invalid input file size\n"), exit (1) ;
    nMachines = InputFileSize >> 2 ;
    }
  else nMachines = MachinesInDatabase ;

  if (Params -> MachineLimitPresent && nMachines > Params -> MachineLimit)
    nMachines = Params -> MachineLimit ;
  }

uint32_t TuringMachineReader::Next (uint8_t* MachineSpec)
  {
  if (MachinesRead >= nMachines) printf ("Invalid read of machine spec\n"), exit (1) ;
  uint32_t MachineIndex = 0 ;

  if (!Params -> MachineSpec.empty())
    ConvertToBinary (MachineSpec, (const uint8_t*)Params -> MachineSpec.c_str()) ;
  else
    {
    if (Params -> TestMachinePresent) MachineIndex = Params -> TestMachine ;
    else if (fpInput) MachineIndex = Read32 (fpInput) ;
    else MachineIndex = MachinesRead ;

    Read (MachineIndex, MachineSpec) ;
    }
  CheckMachineSpec (MachineSpec) ;

  MachinesRead++ ;
  return MachineIndex ;
  }

void TuringMachineReader::Read (uint32_t MachineIndex, uint8_t* MachineSpec, uint32_t n)
  {
  if (MachineIndex + n > MachinesInDatabase)
    printf ("Invalid machine index %d\n", MachineIndex + n - 1), exit (1) ;
  uint8_t SpecFromFile[MAX_MACHINE_SPEC_SIZE] ;
  off64_t FileOffset = MachineIndex ;
  FileOffset *= SpecSizeInFile ;
  if (Params -> BinaryMachineSpecs)
    FileOffset += SpecSizeInFile ; // Skip 30-byte header

  // File offset can exceed 2^31, so we need to use fseek064:
  if (fseeko64 (fpDatabase, FileOffset, SEEK_SET))
    printf ("Seek error for machine %d\n", MachineIndex), exit (1) ;

  ::Read (fpDatabase, SpecFromFile, SpecSizeInFile) ;

  if (!Params -> BinaryMachineSpecs) ConvertToBinary (MachineSpec, SpecFromFile) ;
  else memcpy (MachineSpec, SpecFromFile, MachineSpecSize) ;

  CheckMachineSpec (MachineSpec) ;
  }

void TuringMachineReader::ConvertToBinary (uint8_t* BinSpec, const uint8_t* TextSpec)
  {
  for (uint32_t i = 0 ; i < Params -> MachineStates ; i++)
    {
    for (int j = 0 ; j < 2 ; j++)
      {
      if (*TextSpec == '-')
        {
        BinSpec[0] = BinSpec[1] = BinSpec[2] = 0 ;
        TextSpec += 3 ;
        BinSpec += 3 ;
        }
      else
        {
        *BinSpec++ = *TextSpec++ - '0' ; // Write
        switch (*TextSpec++)             // Move
          {
          case 'R': *BinSpec++ = 0 ; break ;
          case 'L': *BinSpec++ = 1 ; break ;
          default: printf ("Invalid machine spec\n") ; exit (1) ;
          }
        *BinSpec++ = *TextSpec++ - '@' ; // State
        }
      }
    TextSpec++ ; // Skip underscore separator
    }
  }

void TuringMachineReader::CheckMachineSpec (uint8_t* MachineSpec)
  {
  for (uint32_t i = 0 ; i < 2 * Params -> MachineStates ; i++)
    {
    if (*MachineSpec++ > 1)
      printf ("Invalid Write field in machine spec\n"), exit (1) ;
    if (*MachineSpec++ > 1)
      printf ("Invalid Move field in machine spec\n"), exit (1) ;
    if (*MachineSpec++ > Params -> MachineStates)
      printf ("Invalid Move field in machine spec\n"), exit (1) ;
    }
  }
