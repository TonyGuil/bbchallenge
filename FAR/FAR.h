// FAR.h
//
// The Decider and Verifier can handle DeciderTag FAR_DFA (10) and FAR_DFA_NFA (11).
//
// To specify FAR_DFA_NFA Decider output, use command-line parameter -F.
//
// The same -F parameter to the Verifier means: if the VerificationInfo tag is
// FAR_DFA_NFA, then after verifying, reconstruct the NFA from the DFA and check it
// against the NFA in the VerificationInfo.

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "BoolAlgebra.h"
#include "../TuringMachine.h"

#define VERIFY_ERROR() \
  printf ("\n#%d: Error at line %d in %s (fpos 0x%lX)\n", \
  SeedDatabaseIndex, __LINE__, __FUNCTION__, ftell (fp)), exit (1)

class FiniteAutomataReduction : public TuringMachineSpec
  {
public:
  static const uint32_t MaxDFA_States = 9 ;
  static const uint32_t MaxNFA_States = MAX_MACHINE_STATES * MaxDFA_States + 1 ;
  static const uint32_t MaxVerifEntryLen = 17 +
    2 * MaxDFA_States + (2 * MaxNFA_States + 1) * ((MaxNFA_States + 7) >> 3) ;

  using Vector = BoolVector<MaxNFA_States> ;
  using Matrix = BoolMatrix<MaxNFA_States> ;

  FiniteAutomataReduction (uint32_t MachineStates, FILE* fp, bool CheckNFA, bool TraceOutput = false)
  : TuringMachineSpec (MachineStates)
  , fp (fp)
  , CheckNFA (CheckNFA)
  , TraceOutput (TraceOutput)
    {
    memset (MachineCount, 0, sizeof (MachineCount)) ;
    }

  bool RunDecider (uint32_t DFA_States, const uint8_t* MachineSpec, uint8_t* VerificationEntry) ;
  bool ExtendNFA (const uint8_t* MachineSpec, Matrix R[2], Vector& a, uint32_t k) ;
  void MakeVerificationEntry (uint8_t* VerificationEntry) ;

  void Verify (const uint8_t* MachineSpec) ;
  void ReadVerificationInfo() ;

  void SetDFA_States (uint32_t n)
    {
    DFA_States = n ;
    NFA_States = MachineStates * DFA_States + 1 ;
    HALT_State = NFA_States - 1 ;
    }
  void ReconstructNFA (const uint8_t* MachineSpec) ;

  DeciderTag Tag = DeciderTag::FAR_DFA_ONLY ;
  uint8_t Direction ;      // 0 if left-to-right, 1 if right-to-left
  uint32_t DFA_States ;
  uint32_t NFA_States ;
  uint32_t HALT_State ;

  uint8_t DFA[MaxDFA_States][2] ;
  Matrix R[2] ; // Transition matrices
  Vector a ;

  FILE* fp = nullptr ;
  bool CheckNFA ;
  bool TraceOutput ;

  uint32_t MachineCount[MaxDFA_States + 1] ; // for reporting statistics

  void DumpDFA() ;
  void DumpNFA() ;
  } ;
