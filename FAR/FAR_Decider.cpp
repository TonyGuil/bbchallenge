#include <algorithm>
#include "FAR.h"

bool FiniteAutomataReduction::RunDecider (uint32_t DFA_States,
  const uint8_t* MachineSpec, uint8_t* VerificationEntry)
  {
  if (TraceOutput) printf ("#%d\n", SeedDatabaseIndex) ;

  this -> DFA_States = DFA_States ;
  NFA_States = 5 * DFA_States + 1 ;
  HALT_State = NFA_States - 1 ;

  Matrix RStack[MaxDFA_States + 1][2] ;
  Vector aStack[MaxDFA_States + 1] ;

  for (uint32_t i = 0 ; i <= DFA_States ; i++)
    {
    RStack[i][0].SetBitWidth (NFA_States) ;
    RStack[i][1].SetBitWidth (NFA_States) ;
    aStack[i].SetBitWidth (NFA_States) ;
    }

  for (Direction = 0 ; Direction <= 1 ; Direction++)
    {
    RStack[0][0].SetBitWidth (NFA_States) ;
    RStack[0][1].SetBitWidth (NFA_States) ;
    aStack[0].SetBitWidth (NFA_States) ;

    // 5'
    RStack[0][0][HALT_State].SetBit (HALT_State) ;
    RStack[0][1][HALT_State].SetBit (HALT_State) ;
  
    // 7'
    const uint8_t* p = MachineSpec ;
    for (uint8_t f = 0 ; f < 5 ; f++) // A-E
      for (uint8_t r = 0 ; r <= 1 ; r++)
        {
        if (p[2] == 0) // HALT transition
          for (uint32_t i = 0 ; i < DFA_States ; i++)
            RStack[0][r][5*i + f].SetBit (HALT_State) ;
        p += 3 ;
        }
  
    aStack[0].SetBit (HALT_State) ;
  
    uint32_t k = 1 ;
    uint8_t* t = DFA[0] ;
    memset (t, 0, 2 * DFA_States) ;
    uint8_t m[MaxDFA_States] ;
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

          Verify (MachineSpec) ;
          MakeVerificationEntry (VerificationEntry) ;
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
  // 9'
  const uint8_t* p = MachineSpec ;
  uint32_t i = (k - 1) / 2 ;
  uint32_t w = (k - 1) & 1 ;
  for (uint8_t f = 0 ; f < 5 ; f++) // A-E
    for (uint8_t r = 0 ; r <= 1 ; r++)
      {
      if (p[2] != 0 && p[1] == Direction && p[0] == w) // Right-rule
        {
        uint8_t t = p[2] - 1 ; // Convert state from 1-based to 0-based
        uint32_t d = DFA[i][w] ;
        R[r][5*i + f].SetBit (5*d + t) ;
        }
      p += 3 ;
      }

  // 8'
  for ( ; ; )
    {
    bool Changed = false ;
    p = MachineSpec ;
    for (uint8_t f = 0 ; f < 5 ; f++) // A-E
      for (uint8_t r = 0 ; r <= 1 ; r++)
        {
        if (p[2] != 0 && p[1] != Direction) // Left-rule
          {
          uint8_t t = p[2] - 1 ; // Convert state from 1-based to 0-based
          w = p[0] ;
          for (uint32_t j = 1 ; j <= k ; j++)
            {
            i = (j - 1) / 2 ;
            uint32_t b = (j - 1) & 1 ;
            uint32_t d = DFA[i][b] ;
            Vector v = R[b][5*i + t] * R[w] ;
            if (!(R[r][5*d + f] >= v))
              {
              R[r][5*d + f] += v ;
              Changed = true ;
              }
            }
          }
        p += 3 ;
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

void FiniteAutomataReduction::MakeVerificationEntry (uint8_t* VerificationEntry)
  {
  if (VerificationEntry == 0) return ;

  Save32 (VerificationEntry, SeedDatabaseIndex) ;
  Save32 (VerificationEntry + 4, (uint32_t)Tag) ;
  if (Tag == DeciderTag::FAR_DFA_ONLY)
    {
    Save32 (VerificationEntry + 8, 2 * DFA_States + 1) ;
    VerificationEntry[12] = Direction ;
    memcpy (VerificationEntry + 13, DFA, 2 * DFA_States) ;
    }
  else
    {
    uint32_t nBytes = (NFA_States + 7) >> 3 ;
    Save32 (VerificationEntry + 8,
      5 + 2 * DFA_States + (2 * NFA_States + 1) * nBytes) ;
    VerificationEntry[12] = Direction ;
    Save16 (VerificationEntry + 13, DFA_States) ;
    Save16 (VerificationEntry + 15, NFA_States) ;
    VerificationEntry += 17 ;
    memcpy (VerificationEntry, DFA, 2 * DFA_States) ;
    VerificationEntry += 2 * DFA_States ;
    for (uint32_t r = 0 ; r <= 1 ; r++)
      for (uint32_t i = 0 ; i < NFA_States ; i++)
        {
        memcpy (VerificationEntry, R[r][i].d, nBytes) ;
        VerificationEntry += nBytes ;
        }
    memcpy (VerificationEntry, a.d, nBytes) ;
    }
  }
