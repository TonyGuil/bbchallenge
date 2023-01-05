#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string>

#include "../bbchallenge.h"

#define VERIFY_ERROR() \
  printf ("\n#%d: Error at line %d in %s (fpos 0x%lX)\n", \
  SeedDatabaseIndex, __LINE__, __FUNCTION__, ftell (fp)), exit (1)

static const int MaxStates = 128 ; // Max in the dvf is 73
static const int MaxBitmapWords = (MaxStates + 31) >> 5 ; // in 32-bit words
static const int MaxBitmapBytes = 4 * MaxBitmapWords ;

//
// Command-line parameters
//

class CommandLineParams
  {
public:
  static std::string SeedDatabaseFile ;
  static std::string VerificationFile ;
  static uint32_t TestMachine ; static bool TestMachinePresent ;
  static void Parse (int argc, char** argv) ;
  static void PrintHelpAndExit [[noreturn]] (int status) ;
  } ;

std::string CommandLineParams::SeedDatabaseFile ;
std::string CommandLineParams::VerificationFile ;
uint32_t CommandLineParams::TestMachine ; bool CommandLineParams::TestMachinePresent ;

// Bitmap class: a boolean vector
//
// To keep things simple and fast, every Bitmap contains a 16-byte array,
// stored as four 32-bit words

class NFA_Verifier ;

class Bitmap
  {
public:
  Bitmap() { memset (d, 0, sizeof (d)) ; }
  Bitmap (const Bitmap& b) { memcpy (d, b.d, sizeof (d)) ; }

  bool Read (const NFA_Verifier* Verifier) ;
  void SetBit (uint32_t bitnum)
    {
    d[bitnum >> 5] |= 1 << (bitnum & 31) ;
    }
  bool BitSet (uint32_t bitnum) const ;

  Bitmap& operator= (const Bitmap& b) ;
  bool operator== (const Bitmap& b) const
    {
    return memcmp (d, b.d, sizeof (d)) == 0 ;
    }
  bool operator!= (const Bitmap& b) const
    {
    return !operator== (b) ;
    }

  // Boolean operations
  Bitmap& operator+= (const Bitmap& b) ;   // boolean OR
  Bitmap& operator-= (const Bitmap& b) ;   // boolean AND NOT
  bool operator* (const Bitmap& b) const ; // boolean AND

  // a <= b means that every 1-bit in a is also set in b. Similarly for >=.
  // So it is not the case that for all a,b, a <= b || b <= a
  bool operator<= (const Bitmap& b) const ;
  bool operator>= (const Bitmap& b) const ;

  uint32_t d[MaxBitmapWords] ;
  } ;

//
// NFA_Verifier class
//

class NFA_Verifier
  {
public:
  uint32_t SeedDatabaseIndex ;

  static uint32_t nStates ; // Number of NFA states (static so the user-defined operators see it)

  uint8_t Direction ;       // 0 if left-to-right, 1 if right-to-left
  uint32_t BitmapBytes ;    // Number of bytes
  uint32_t BitmapWords ;    // Number of 32-bit words
  Bitmap T[7][MaxStates] ;  // Transition matrix
  Bitmap a ;                // Accept vector a
  Bitmap s ;                // Steady-state vector s

  void Verify (const uint8_t* MachineSpec) ;
  void ReadVerificationInfo() ;

  void Multiply (Bitmap* U, const Bitmap* V, const Bitmap* W) ;
  bool GreaterOrEqual (const Bitmap* U, const Bitmap* V) ;

  FILE* fp ;
  } ;

uint32_t NFA_Verifier::nStates ;

// User-defined operators to post-/pre-multiply a Bitmap by a matrix
Bitmap operator* (const Bitmap& v, const Bitmap* A) ; // Compute v A
Bitmap operator* (const Bitmap* A, const Bitmap& v) ; // Compute A v

int main (int argc, char** argv)
  {
  CommandLineParams::Parse (argc, argv) ;
  TuringMachineReader Reader (CommandLineParams::SeedDatabaseFile.c_str()) ;

  // Open the Decider Verification File
  NFA_Verifier Verifier ;
  Verifier.fp = fopen (CommandLineParams::VerificationFile.c_str(), "rb") ;
  if (Verifier.fp == 0)
    printf ("File \"%s\" not found\n", CommandLineParams::VerificationFile.c_str()), exit (1) ;

  // First uint in dvf is number of machines
  uint32_t nMachines = Read32 (Verifier.fp) ;
  int LastPercent = -1 ;
  uint8_t MachineSpec[MACHINE_SPEC_SIZE] ;

  if (CommandLineParams::TestMachinePresent)
    {
    // Verify a single machine (for development purposes)
    for ( ; ; )
      {
      Verifier.SeedDatabaseIndex = Read32 (Verifier.fp) ;
      if (DeciderTag (Read32 (Verifier.fp)) != DeciderTag::NFA)
        printf ("\n%d: Unrecognised DeciderTag\n", Verifier.SeedDatabaseIndex), exit (1) ;
      if (Verifier.SeedDatabaseIndex < CommandLineParams::TestMachine)
        {
        Verifier.ReadVerificationInfo() ;
        continue ;
        }
      if (Verifier.SeedDatabaseIndex != CommandLineParams::TestMachine)
        printf ("Machine %d not found\n", CommandLineParams::TestMachine), exit (1) ;
      Reader.Read (Verifier.SeedDatabaseIndex, MachineSpec) ;
      Verifier.Verify (MachineSpec) ;
      exit (0) ;
      }
    }

  clock_t Timer = clock() ;

  for (uint32_t Entry = 0 ; Entry < nMachines ; Entry++)
    {
    int Percent = ((Entry + 1) * 100LL) / nMachines ;
    if (Percent != LastPercent)
      {
      printf ("\r%d%%", Percent) ;
      LastPercent = Percent ;
      }

    // Read SeedDatabaseIndex and Tag from dvf
    Verifier.SeedDatabaseIndex = Read32 (Verifier.fp) ;
    if (DeciderTag (Read32 (Verifier.fp)) != DeciderTag::NFA)
      printf ("\n%d: Unrecognised DeciderTag\n", Verifier.SeedDatabaseIndex), exit (1) ;

    // Read the machine spec from the seed database file
    Reader.Read (Verifier.SeedDatabaseIndex, MachineSpec) ;

    // Verify it
    Verifier.Verify (MachineSpec) ;
    }

  Timer = clock() - Timer ;

  if (fread (MachineSpec, 1, 1, Verifier.fp) != 0)
    printf ("File too long!\n"), exit (1) ;
  fclose (Verifier.fp) ;
  printf ("\n%d NFAs verified\n", nMachines) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;
  }

void NFA_Verifier::Verify (const uint8_t* MachineSpec)
  {
  ReadVerificationInfo() ;

  // Check each of the cnoditions in Theorem 17

  // (1) q0 T0 = q0
  Bitmap q0 ; q0.SetBit (0) ;
  if (T[0][0] != q0) VERIFY_ERROR() ;

  // (2) T0 a = a
  if (T[0] * a  != a) VERIFY_ERROR() ;

  // (3) q0 TA a = 0
  if (T[2][0] * a) VERIFY_ERROR() ;

  // (4) sa != 0
  if (!(s * a)) VERIFY_ERROR() ;

  // (5) s T0, s T1 >= s
  if (!(s * T[0] >= s) || !(s * T[1] >= s)) VERIFY_ERROR() ;

  // (6) For all u in {0,1}* : q0 Tu != 0

  Bitmap Reached = T[0][0] ; // Vector of all states reachable from q0 via T[0] and T[1]
  Reached += T[1][0] ;
  Bitmap Checked ;
  Bitmap Zero ;
  for ( ; ; )
    {
    if (Checked == Reached) break ; // No new states reached
    Bitmap New = Reached ; New -= Checked ;
    for (uint32_t i = 0 ; i < nStates ; i++) if (New.BitSet (i))
      {
      if (T[0][i] == Zero || T[1][i] == Zero) VERIFY_ERROR() ;
      Reached += T[0][i] ;
      Reached += T[1][i] ;
      Checked.SetBit (i) ;
      }
    }

  Bitmap M1[MaxStates] ;
  Bitmap M2[MaxStates] ;
  Bitmap M3[MaxStates] ;

  const uint8_t* p = MachineSpec ; 
  for (uint8_t f = 2 ; f <= 6 ; f++) // A-E
    for (uint8_t r = 0 ; r <= 1 ; r++)
      {
      if (p[2] == 0) // HALT transition
        {
        // (7) For all q in span{q0 Tu} - {0} : q Tf Tr >= s
        Multiply (M1, T[f], T[r]) ;
        for (uint32_t i = 0 ; i < nStates ; i++) if (Reached.BitSet (i))
          if (!(M1[i] >= s)) VERIFY_ERROR() ;
        }
      else
        {
        uint8_t w = p[0] ;
        uint8_t t = p[2] + 1 ; // Convert state from 1-based to 2-based

        if (p[1] == Direction) // Right-rule (after possible reflection)
          {
          // (9) Tf Tr >= Tw Tt
          Multiply (M1, T[f], T[r]) ;
          Multiply (M2, T[w], T[t]) ;
          if (!GreaterOrEqual (M1, M2)) VERIFY_ERROR() ;
          }
        else // Left-rule
          {
          // (8) Tb Tf Tr >= Tt Tb Tw (for b in { 0, 1 } )
          for (uint32_t b = 0 ; b <= 1 ; b++)
            {
            Multiply (M1, T[f], T[r]) ;  // M1 = Tf Tr
            Multiply (M2, T[b], M1) ;    // M2 = Tb Tf Tr
  
            Multiply (M1, T[b], T[w]) ;  // M1 = Tb Tw
            Multiply (M3, T[t], M1) ;    // M3 = Tt Tb Tw

            if (!GreaterOrEqual (M2, M3))
              VERIFY_ERROR() ;
            }
          }
        }

      p += 3 ;
      }
  }

void NFA_Verifier::ReadVerificationInfo()
  {
  uint32_t InfoLen = Read32 (fp) ;

  Direction = Read8u (fp) ;
  nStates = Read8u (fp) ;
  if (nStates > MaxStates) VERIFY_ERROR() ;
  BitmapBytes = (nStates + 7) >> 3 ;
  BitmapWords = (nStates + 31) >> 5 ;
  if (InfoLen != (7 * nStates + 2) * BitmapBytes + 2) VERIFY_ERROR() ;

  for (uint32_t Symbol = 0 ; Symbol < 7 ; Symbol++)
    for (uint32_t Row = 0 ; Row < nStates ; Row++)
      if (!T[Symbol][Row].Read (this)) VERIFY_ERROR() ;
  if (!a.Read (this)) VERIFY_ERROR() ;
  if (!s.Read (this)) VERIFY_ERROR() ;
  }

bool Bitmap::Read (const NFA_Verifier* Verifier)
  {
  memset (d, 0, MaxBitmapBytes) ;
  return fread (d, Verifier -> BitmapBytes, 1, Verifier -> fp) == 1 ;
  }

bool Bitmap::BitSet (uint32_t bitnum) const 
  {
  return (d[bitnum >> 5] & (1 << (bitnum & 31))) != 0 ;
  }

Bitmap& Bitmap::operator= (const Bitmap& b)
  {
  memcpy (d, b.d, MaxBitmapBytes) ;
  return *this ;
  }

Bitmap& Bitmap::operator+= (const Bitmap& b)
  {
  for (uint32_t i = 0 ; i < MaxBitmapWords ; i++)
    d[i] |= b.d[i] ;
  return *this ;
  }

Bitmap& Bitmap::operator-= (const Bitmap& b)
  {
  for (uint32_t i = 0 ; i < MaxBitmapWords ; i++)
    d[i] &= ~b.d[i] ;
  return *this ;
  }

bool Bitmap::operator* (const Bitmap& b) const
  {
  for (uint32_t i = 0 ; i < MaxBitmapWords ; i++)
    if (d[i] & b.d[i]) return true ;
  return false ;
  }

bool Bitmap::operator<= (const Bitmap& b) const
  {
  for (uint32_t i = 0 ; i < MaxBitmapWords ; i++)
    if (d[i] & ~b.d[i]) return false ;
  return true ;
  }

bool Bitmap::operator>= (const Bitmap& b) const
  {
  for (uint32_t i = 0 ; i < MaxBitmapWords ; i++)
    if (~d[i] & b.d[i]) return false ;
  return true ;
  }

// Bitmap operator* (const Bitmap& v, const Bitmap* A)
//
// Returns v * A

Bitmap operator* (const Bitmap& v, const Bitmap* A)
  {
  Bitmap result ;
  for (uint32_t i = 0 ; i < NFA_Verifier::nStates ; i++)
    if (v.BitSet (i)) result += A[i] ;
  return result ;
  }

// Bitmap operator* (const Bitmap* A, const Bitmap& v)
//
// Returns A * v

Bitmap operator* (const Bitmap* A, const Bitmap& v)
  {
  Bitmap result ;
  for (uint32_t i = 0 ; i < NFA_Verifier::nStates ; i++)
    if (v * A[i]) result.SetBit (i) ;
  return result ;
  }

// void NFA_Verifier::Multiply (Bitmap* U, const Bitmap* V, const Bitmap* W)
//
// U = V * W

void NFA_Verifier::Multiply (Bitmap* U, const Bitmap* V, const Bitmap* W)
  {
  for (uint32_t i = 0 ; i < NFA_Verifier::nStates ; i++)
    U[i] = V[i] * W ;
  }

bool NFA_Verifier::GreaterOrEqual (const Bitmap* U, const Bitmap* V)
  {
  for (uint32_t i = 0 ; i < NFA_Verifier::nStates ; i++)
    if (!(U[i] >= V[i]))
      return false ;
  return true ;
  }

void CommandLineParams::Parse (int argc, char** argv)
  {
  if (argc == 1) PrintHelpAndExit (0) ;

  for (argc--, argv++ ; argc ; argc--, argv++)
    {
    if (argv[0][0] != '-') printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
    switch (toupper (argv[0][1]))
      {
      case 'D':
        if (argv[0][2] == 0) printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
        SeedDatabaseFile = std::string (&argv[0][2]) ;
        break ;

      case 'V':
        if (argv[0][2] == 0) printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
        VerificationFile = std::string (&argv[0][2]) ;
        break ;

      case 'X':
        TestMachine = atoi (&argv[0][2]) ;
        TestMachinePresent = true ;
        break ;

      default:
        printf ("Invalid parameter \"%s\"\n", argv[0]) ;
        PrintHelpAndExit (1) ;
      }
    }

  if (VerificationFile.empty()) printf ("Verification file not specified\n"), PrintHelpAndExit (1) ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf (R"*RAW*(
VerifyNFAs <param> <param>...
  <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
           -V<verification file>  Decider Verification File for decided machines
           -X<test machine>       Machine to test
)*RAW*") ;
  exit (status) ;
  }
