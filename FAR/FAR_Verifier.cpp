#include "../bbchallenge.h"
#include "FAR.h"

void FiniteAutomataReduction::Verify (const uint8_t* MachineSpec)
  {
  if (Tag == DeciderTag::FAR_DFA_ONLY) ReconstructNFA (MachineSpec) ;

  for (uint32_t i = 0 ; i < DFA_States ; i++)
    if (DFA[i][0] >= DFA_States || DFA[i][1] >= DFA_States) VERIFY_ERROR() ;

  // (1) q0 T[0] = q0
  if (DFA[0][0] != 0) VERIFY_ERROR() ;

  // (2) T[0] a = a
  if (R[0] * a != a) VERIFY_ERROR() ;

  // (3) q0 T[A] a = 0
  if (R[0][0] * a) VERIFY_ERROR() ;

  // (4) s a != 0
  if (!a[HALT_State]) VERIFY_ERROR() ;

  // (5') R[r] >= |HALT> <HALT|
  if (!R[0][HALT_State][HALT_State]) VERIFY_ERROR() ;
  if (!R[1][HALT_State][HALT_State]) VERIFY_ERROR() ;

  // (6) is vacuously satisfied

  const uint8_t* p = MachineSpec ; 
  for (uint8_t f = 0 ; f < 5 ; f++) // A-E
    for (uint8_t r = 0 ; r <= 1 ; r++)
      {
      if (p[2] == 0) // HALT transition
        {
        // (7')
        for (uint32_t i = 0 ; i < DFA_States ; i++)
          if (!R[r][5*i + f][HALT_State])
            VERIFY_ERROR() ;
        }
      else
        {
        uint8_t w = p[0] ;
        uint8_t t = p[2] - 1 ; // Convert state from 1-based to 0-based

        if (p[1] == Direction) // Right-rule (after possible reflection)
          {
          // (9')
          for (uint32_t i = 0 ; i < DFA_States ; i++)
            {
            uint32_t d = DFA[i][w] ;
            if (!R[r][5*i + f][5*d + t]) VERIFY_ERROR() ;
            }
          }
        else // Left-rule
          {
          // (8')
          for (uint32_t i = 0 ; i < DFA_States ; i++)
            for (uint32_t b = 0 ; b <= 1 ; b++)
              {
              uint32_t d = DFA[i][b] ;

              // We need to check that R[r] >= |d,f> <i,t| R[b] R[w]
              //
              // A = |d,f> <i,t| is a matrix of zeroes except for the entry in
              // row 5*d + f, column 5*i + t. Pre-multiplying a matrix B by A
              // has the effect of moving row 5*i + t of B to row 5*d + f,
              // and filling the rest of B with zeroes.
              //
              // So we need to check that row 5*d + f of R[r] is >= row 5*i + t
              // of R[b] R[w]. And this row is just R[b][5*i + t] * R[w]:

              if (!(R[r][5*d + f] >= R[b][5*i + t] * R[w]))
                VERIFY_ERROR() ;
              }
          }
        }
      p += 3 ;
      }

  if (CheckNFA && Tag == DeciderTag::FAR_DFA_NFA)
    {
    FiniteAutomataReduction::Matrix RCopy[2] ;
    FiniteAutomataReduction::Vector aCopy ;
    RCopy[0] = R[0] ;
    RCopy[1] = R[1] ;
    aCopy = a ;
  
    ReconstructNFA (MachineSpec) ;
  
    if (R[0] != RCopy[0] || R[1] != RCopy[1] || a != aCopy)
      VERIFY_ERROR() ;
    }

  MachineCount[DFA_States]++ ;
  }

void FiniteAutomataReduction::ReadVerificationInfo()
  {
  Tag = DeciderTag (Read32 (fp)) ;
  if (Tag != DeciderTag::FAR_DFA_ONLY && Tag != DeciderTag::FAR_DFA_NFA) VERIFY_ERROR() ;
  uint32_t InfoLen = Read32 (fp) ;

  switch (Tag)
    {
    case DeciderTag::FAR_DFA_ONLY:
      Direction = Read8u (fp) ;
      DFA_States = (InfoLen - 1) / 2 ;
      NFA_States = 5 * DFA_States + 1 ;
      HALT_State = NFA_States - 1 ;
      if (fread (DFA, DFA_States, 2, fp) != 2) VERIFY_ERROR() ;
      break ;

    case DeciderTag::FAR_DFA_NFA:
      Direction = Read8u (fp) ;
      DFA_States = Read16u (fp) ;
      if (DFA_States > MaxDFA_States) VERIFY_ERROR() ;
      NFA_States = Read16u (fp) ;
      if (NFA_States > MaxNFA_States) VERIFY_ERROR() ;
      if (NFA_States != 5 * DFA_States + 1) VERIFY_ERROR() ;
    
      if (InfoLen != 5 + 2 * DFA_States + (2 * NFA_States + 1) * ((NFA_States + 7) >> 3))
        VERIFY_ERROR() ;
      HALT_State = NFA_States - 1 ;
    
      if (fread (DFA, DFA_States, 2, fp) != 2) VERIFY_ERROR() ;
      for (uint32_t r = 0 ; r <= 1 ; r++)
        R[r].Read (fp, NFA_States) ;
      a.Read (fp, NFA_States) ;
      break ;

    default: VERIFY_ERROR() ;
    }
  }

void FiniteAutomataReduction::ReconstructNFA (const uint8_t* MachineSpec)
  {
  R[0].SetBitWidth (NFA_States) ;
  R[1].SetBitWidth (NFA_States) ;
  a.SetBitWidth (NFA_States) ;

  // 5'
  R[0][HALT_State].SetBit (HALT_State) ;
  R[1][HALT_State].SetBit (HALT_State) ;

  // 7'
  const uint8_t* p = MachineSpec ;
  for (uint8_t f = 0 ; f < 5 ; f++) // A-E
    for (uint8_t r = 0 ; r <= 1 ; r++)
      {
      if (p[2] == 0) // HALT transition
        for (uint32_t i = 0 ; i < DFA_States ; i++)
          R[r][5*i + f].SetBit (HALT_State) ;
      p += 3 ;
      }

  a.SetBit (HALT_State) ;

  for (uint32_t k = 1 ; k <= 2 * DFA_States ; k++)
    {
    // 9'
    p = MachineSpec ;
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

    if (R[0][0] * a) printf ("Reproduction failed\n"), exit (1) ;
    }
  }

void FiniteAutomataReduction::DumpDFA()
  {
  for (uint32_t i = 0 ; i < DFA_States ; i++)
    printf ("(%d %d) ", DFA[i][0], DFA[i][1]) ;
  printf ("\n") ;
  }

void FiniteAutomataReduction::DumpNFA()
  {
  int nDigits = (NFA_States + 3) >> 2 ;
  for (uint32_t r = 0 ; r <= 1 ; r++)
    {
    for (uint32_t i = 0 ; i < NFA_States ; i++)
      printf ("%0*X ", nDigits, R[r][i].d[0]) ;
    printf ("\n") ;
    }
  printf ("%0*X\n", nDigits, a.d[0]) ;
  }
