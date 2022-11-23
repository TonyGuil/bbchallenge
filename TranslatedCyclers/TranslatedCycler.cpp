// TranslatedCyclers.cpp
//
// Decider for TranslatedCyclers
//
// To run:
//   TranslatedCyclers <param> <param>...
//     <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
//              -I<input file>         Input file: list of machines to be analysed (umf)
//              -V<verification file>  Output file: verification data for decided machines (dvf)
//              -U<undecided file>     Output file: remaining undecided machines (umf)
//              -T<time limit>         Max no. of steps
//              -S<space limit>        Max absolute value of tape head
//              -W<workspace size>     Tape history workspace
//              -M<threads>            Number of threads to use
//
// Parameters -I, -V, -U, and -T are compulsory. Other parameters were mainly for tuning
// during development, and can be omitted.
//
// Format of Decider Verification File (32-bit big-endian integers, signed or unsigned):
//
// DeciderSpecificInfo format:
//   uint nEntries
//   VerificationEntry[nEntries]
// 
//   VerificationEntry format:
//     uint SeedDatabaseIndex
//     uint DeciderType     -- 2 = TranslatedCyclers (translated to the right)
//                          -- 3 = TranslatedCyclers (translated to the left)
//     uint InfoLength = 32 -- Length of decider-specific info
//     -- DeciderSpecificInfo
//     -- An initial configuration matches a final configuration translated left or right.
//     -- Leftmost and Rightmost are for the convenience of the Verifier, and not strictly necessary.
//     int Leftmost            -- Leftmost tape head position
//     int Rightmost           -- Rightmost tape head position
//     uint State              -- State of machine in initial and final configurations
//     int InitialTapeHead     -- Tape head in initial configuration
//     int FinalTapeHead       -- Tape head in final configuration
//     uint InitialStepCount   -- Number of steps to reach initial configuration
//     uint FinalStepCount     -- Number of steps to reach final configuration
//     uint MatchLength        -- Length of match
//
// NOTE: This code will NOT decide (non-translated) Cyclers, because it only
// looks for a match when a Leftmost/Rightmost record is broken, not when it
// is simply matched. This saves us a lot of time.

#include "TranslatedCycler.h"

bool TranslatedCycler::Run (const uint8_t* MachineSpec, uint8_t* VerificationEntry)
  {
  Initialise (SeedDatabaseIndex, MachineSpec) ;

  uint32_t nLeftRecords = 0 ;
  uint32_t nRightRecords = 0 ;
  memset (LatestLeftRecord, 0, sizeof (LatestLeftRecord)) ;
  memset (LatestRightRecord, 0, sizeof (LatestRightRecord)) ;

  while (StepCount < TimeLimit)
    {
    switch (Step())
      {
      case StepResult::OK: break ;
      case StepResult::OUT_OF_BOUNDS: return false ;
      case StepResult::HALT:
        printf ("Unexpected HALT state reached! %d\n", StepCount) ;
        printf ("SeedIndex = %d, TapeHead = %d\n", Load32 (MachineSpec - 4), TapeHead) ;
        exit (1) ;
      }

    if (RecordBroken == 1)
      {
      if (nRightRecords == RecordLimit) return false ;
      RightRecordList[nRightRecords].StepCount = StepCount ;
      RightRecordList[nRightRecords].TapeHead = TapeHead ;
      RightRecordList[nRightRecords].Prev = LatestRightRecord[State] ; ;
      LatestRightRecord[State] = &RightRecordList[nRightRecords] ;
      nRightRecords++ ;
      if (DetectRepetition (LatestRightRecord, State, VerificationEntry))
        {
        if (TraceOutput) printf ("%d\n", SeedDatabaseIndex) ;
        return true ;
        }
      }
    if (RecordBroken == -1)
      {
      if (nLeftRecords == RecordLimit) return false ;
      LeftRecordList[nLeftRecords].StepCount = StepCount ;
      LeftRecordList[nLeftRecords].TapeHead = TapeHead ;
      LeftRecordList[nLeftRecords].Prev = LatestLeftRecord[State] ; ;
      LatestLeftRecord[State] = &LeftRecordList[nLeftRecords] ;
      nLeftRecords++ ;
      if (DetectRepetition (LatestLeftRecord, State, VerificationEntry))
        {
        if (TraceOutput) printf ("%d\n", SeedDatabaseIndex) ;
        return true ;
        }
      }
    }

  return false ;
  }

bool TranslatedCycler::DetectRepetition (Record* LatestRecord[], uint8_t State, uint8_t* VerificationEntry)
  {
  #define BACKWARD_SCAN_LENGTH 2000
  Record* Workspace[3 * BACKWARD_SCAN_LENGTH] ;
  Record* Latest = LatestRecord[State] ;

  bool Cloned = false ;
  for (int i = 1 ; i <= BACKWARD_SCAN_LENGTH ; i++)
    {
    for (int j = 0 ; j < 3 ; j++)
      {
      if (Latest == 0) return false ;
      Workspace[3 * (i - 1) + j] = Latest ;
      Latest = Latest -> Prev ;
      }

    if (Workspace[i] - Workspace[2 * i] != Workspace[0] - Workspace[i])
      continue ;

    int CycleShift = Workspace[0] -> TapeHead - Workspace[i] -> TapeHead ;
    if (Workspace[i] -> TapeHead - Workspace[2 * i] -> TapeHead != CycleShift)
      continue ;

    uint32_t CycleSteps = Workspace[0] -> StepCount - Workspace[i] -> StepCount ;
    if (Workspace[i] -> StepCount - Workspace[2 * i] -> StepCount != CycleSteps)
      continue ;

    if (!Cloned)
      {
      *Clone = *this ;
      Clone -> Leftmost = Clone -> Rightmost = Clone -> TapeHead ;
      Clone -> StepCount = 0 ;
      Cloned = true ;
      }

    while (Clone -> StepCount < CycleSteps)
      if (Clone -> Step() != StepResult::OK) return false ;
    if (Clone -> RecordBroken == 0) return false ; // We must end on a record
    if (Clone -> StepCount < CycleSteps) continue ;
    if (Clone -> State != State || Clone -> Tape[Clone -> TapeHead] != Tape[TapeHead])
      continue ;
    if (Clone -> TapeHead != TapeHead + CycleShift)
      continue ;

    uint32_t nCells ;
    if (CycleShift > 0)
      {
      if (Clone -> TapeHead != Clone -> Rightmost)
        continue ;
      nCells = TapeHead - Clone -> Leftmost + 1 ;
      if (memcmp (Tape + Clone -> Leftmost, Clone -> Tape + Clone -> TapeHead - nCells + 1, nCells))
        continue ;
      }
    else
      {
      if (Clone -> TapeHead != Clone -> Leftmost)
        continue ;
      nCells = Clone -> Rightmost - TapeHead + 1 ;
      if (memcmp (Tape + TapeHead, Clone -> Tape + Clone -> Leftmost, nCells))
        continue ;
      }

    if (VerificationEntry)
      {
      Save32 (VerificationEntry, SeedDatabaseIndex) ;
      if (CycleShift < 0)
        Save32 (VerificationEntry + 4, uint32_t (DeciderTag::TRANSLATED_CYCLER_LEFT)) ;
      else
        Save32 (VerificationEntry + 4, uint32_t (DeciderTag::TRANSLATED_CYCLER_RIGHT)) ;
      Save32 (VerificationEntry + 8, VERIF_INFO_LENGTH) ;

      // DeciderSpecificInfo
      Save32 (VerificationEntry + 12, std::min (Leftmost, Clone -> Leftmost)) ;
      Save32 (VerificationEntry + 16, std::max (Rightmost, Clone -> Rightmost)) ;
      Save32 (VerificationEntry + 20, State) ;
      Save32 (VerificationEntry + 24, TapeHead) ;
      Save32 (VerificationEntry + 28, Clone -> TapeHead) ;
      Save32 (VerificationEntry + 32, StepCount) ;
      Save32 (VerificationEntry + 36, StepCount + CycleSteps) ;
      Save32 (VerificationEntry + 40, nCells) ;
      }

    if (i > MaxStat)
      {
      MaxStat = i ;
      MaxStatMachine = SeedDatabaseIndex ;
      }

    return true ;
    }

  return false ;
  }
