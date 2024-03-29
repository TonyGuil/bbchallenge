#include <algorithm>
#include "FAR.h"

bool FiniteAutomataReduction::RunDecider (uint32_t DFA_States, const uint8_t* MachineSpec, uint8_t* VerificationEntry)
  {
  if (TraceOutput) printf ("#%d\n", SeedDatabaseIndex) ;

  *VerificationEntry = 0xFF ; // i.e. not decided

  SetDFA_States (DFA_States) ;

  Matrix RStack[2 * MaxDFA_States + 1][2] ;
  Vector aStack[2 * MaxDFA_States + 1] ;

  for (uint32_t i = 0 ; i <= DFA_States ; i++)
    {
    RStack[i][0].SetBitWidth (NFA_States) ;
    RStack[i][1].SetBitWidth (NFA_States) ;
    aStack[i].SetBitWidth (NFA_States) ;
    }

  for (Direction = 0 ; Direction <= 1 ; Direction++)
    {
    // 5'
    RStack[0][0][HALT_State].SetBit (HALT_State) ;
    RStack[0][1][HALT_State].SetBit (HALT_State) ;
  
    // 7'
    const uint8_t* p = MachineSpec ;
    for (uint8_t f = 0 ; f < MachineStates ; f++) // A-E
      for (uint8_t r = 0 ; r <= 1 ; r++)
        {
        if (p[2] == 0) // HALT transition
          for (uint32_t i = 0 ; i < DFA_States ; i++)
            RStack[0][r][MachineStates*i + f].SetBit (HALT_State) ;
        p += 3 ;
        }
  
    aStack[0].SetBit (HALT_State) ;
  
    uint32_t k = 1 ;
    uint8_t* t = DFA[0] ;
    memset (t, 0, 2 * DFA_States) ;
    uint8_t m[2 * MaxDFA_States] ;
    memset (m, 0, 2 * DFA_States) ;
    for ( ; ; )
      {
      RStack[k][0] = RStack[k - 1][0] ;
      RStack[k][1] = RStack[k - 1][1] ;
      aStack[k] = aStack[k - 1] ;

      if (ExtendNFA (MachineSpec, RStack[k], aStack[k], k))
        {
        if (k == 2 * DFA_States)
          {
          // Done
          R[0] = RStack[k][0] ;
          R[1] = RStack[k][1] ;
          a = aStack[k] ;

          Verify (MachineSpec) ; // Should never fail

          VerificationEntry[0] = Direction ;
          memcpy (VerificationEntry + 1, DFA, 2 * DFA_States) ;
          return true ;
          }
        uint32_t q_new = m[k - 1] + 1 ;
        t[k] = (q_new < DFA_States && 2 * q_new - 1 == k) ? q_new : 0 ;
        }
      else
        {
        do
          {
          if (k <= 1) goto Failed ;
          k-- ;
          } while (t[k] > m[k - 1] || t[k] >= DFA_States - 1) ;
        t[k]++ ;
        }
      m[k] = std::max (m[k - 1], t[k]) ;
      k++ ;
      }
Failed:
    ;
    }
  return false ;
  }

bool FiniteAutomataReduction::ExtendNFA (const uint8_t* MachineSpec, Matrix R[2], Vector& a, uint32_t k)
  {
  TuringMachineSpec::Transition T ;
  const uint8_t* p ; 

  // 9'
  uint32_t i = (k - 1) / 2 ;
  uint32_t w = (k - 1) & 1 ;
  uint32_t d = DFA[i][w] ;
  p = MachineSpec ;
  for (uint8_t f = 0 ; f < MachineStates ; f++) // A-E or A-F
    for (uint8_t r = 0 ; r <= 1 ; r++)
      {
      UnpackSpec (&T, p) ; p += 3 ;
      if (T.Next != 0 && T.Move == Direction && T.Write == w) // Right-rule
        {
        uint8_t t = T.Next - 1 ; // Convert state from 1-based to 0-based
        R[r][MachineStates*i + f].SetBit (MachineStates*d + t) ;
        }
      }

  // 8'
  for ( ; ; )
    {
    bool Changed = false ;
    p = MachineSpec ;
    for (uint8_t f = 0 ; f < MachineStates ; f++) // A-E
      for (uint8_t r = 0 ; r <= 1 ; r++)
        {
        UnpackSpec (&T, p) ; p += 3 ;
        if (T.Next != 0 && T.Move != Direction) // Left-rule
          {
          uint8_t t = T.Next - 1 ; // Convert state from 1-based to 0-based
          w = T.Write ;
          for (uint32_t j = 1 ; j <= k ; j++)
            {
            i = (j - 1) / 2 ;
            uint32_t b = (j - 1) & 1 ;
            uint32_t d = DFA[i][b] ;
            Vector v = R[b][MachineStates*i + t] * R[w] ;
            if (!(R[r][MachineStates*d + f] >= v))
              {
              R[r][MachineStates*d + f] += v ;
              Changed = true ;
              }
            }
          }
        }

    if (!Changed) break ;
    }

  Vector aPrev (a) ;
  for ( ; ; )
    {
    a = R[0] * aPrev ;
    if (a == aPrev) break ;
    aPrev = a ;
    }

  return !(R[0][0] * a) ;
  }
