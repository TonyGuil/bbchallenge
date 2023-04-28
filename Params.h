// Params.h
//
// Common command-line parameters for all deciders:
//
//   -N<states>            Machine states (2, 3, 4, 5, or 6)
//   -D<database>          Seed database file (defaults to ../SeedDatabase.bin)
//   -I<input file>        Input file: list of machines to be analysed (default=all machines)
//   -V<verification file> Output file: verification data for decided machines
//   -U<undecided file>    Output file: remaining undecided machines
//   -X<test machine>      Machine to test
//   -M<machine spec>      Compact machine code (ASCII spec) to test
//   -H<threads>           Number of threads to use
//   -L<machine limit>     Max no. of machines to test
//   -O                    Print trace output

#pragma once

#include "Reader.h"
#include <string>

class CommonParams
  {
public:
  uint32_t MachineStates = 5 ;
  std::string DatabaseFilename ;
  std::string VerificationFilename ;
  uint32_t TestMachine ; bool TestMachinePresent = false ;
  std::string MachineSpec ;
  bool BinaryMachineSpecs = true ;

  virtual bool ParseParam (const char* arg) ;
  static uint32_t ParseInt (const char* arg, const char* s) ;
  virtual void CheckParameters() ;
  virtual void OpenFiles()
    {
    fpDatabase = OpenFile (DatabaseFilename, "rb") ;
    }

  virtual void PrintHelp() const ;
  virtual void PrintHelpAndExit (int status) = 0 ;

  static FILE* OpenFile (const std::string& Filename, const char* Access)
    {
    if (Filename.empty()) return nullptr ;
    FILE* fp = fopen (Filename.c_str(), Access) ;
    if (fp == nullptr)
      printf ("Can't open file \"%s\"\n", Filename.c_str()), exit (1) ;
    return fp ;
    }

  virtual bool Verifying() const = 0 ;

  FILE* fpDatabase ;
  FILE* fpInput ;
  FILE* fpVerify ;
  } ;

class DeciderParams : public CommonParams
  {
public:
  std::string InputFilename ;
  std::string UndecidedFilename ;
  uint32_t nThreads ;      bool nThreadsPresent ;
  uint32_t MachineLimit ;  bool MachineLimitPresent ;
  bool TraceOutput = false ;

  virtual bool ParseParam (const char* arg) ;
  virtual void CheckParameters() override ;

  virtual void PrintHelp() const override ;

  virtual void OpenFiles() override
    {
    CommonParams::OpenFiles() ;
    fpInput = OpenFile (InputFilename, "rb") ;
    fpVerify = OpenFile (VerificationFilename, "wb") ;
    fpUndecided = OpenFile (UndecidedFilename, "wb") ;
    }

  virtual bool Verifying() const override { return false ; }

  FILE* fpUndecided ;
  } ;

class VerifierParams : public CommonParams
  {
public:
  virtual void CheckParameters() ;

  virtual void PrintHelp() const override ;

  virtual void OpenFiles()
    {
    fpInput = 0 ; // Not used in Verifiers
    fpVerify = OpenFile (VerificationFilename, "rb") ;
    }

  virtual bool Verifying() const override { return true ; }
  } ;
