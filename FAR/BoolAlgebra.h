// BoolAlgebra.h
//
// Boolean vectors and matrices

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if UINTPTR_MAX > 0xFFFFFFFF // If pointers are 64-bit
  // 64-bit bitmaps
  #define BITS_IN_WORD 64
  #define BIT_MASK     63
  #define BIT_SHIFT    6
  #define ONE          1LL
  typedef uint64_t bitmap_t ;
#else
  // 32-bit bitmaps
  #define BITS_IN_WORD 32
  #define BIT_MASK     31
  #define BIT_SHIFT    5
  #define ONE          1
  typedef uint32_t bitmap_t ;
#endif

#define BOOL_ALGEBRA_ERROR() \
  printf ("\nBoolAlgebra error at line %d in %s\n", \
  __LINE__, __FUNCTION__), exit (1)

template <int MaxBits> class BoolVector
  {
public:
  static const int MaxBytes = (MaxBits + 7) >> 3 ;
  static const int MaxWords = (MaxBits + BIT_MASK) >> BIT_SHIFT ;

  explicit BoolVector (uint32_t BitWidth = 0)
    {
    SetBitWidth (BitWidth) ;
    }
  BoolVector (const BoolVector& v) : BoolVector (v.BitWidth)
    {
if (MaxWords > 1) BOOL_ALGEBRA_ERROR() ;
    memcpy (d, v.d, sizeof (d)) ;
    }
  void SetBitWidth (uint32_t BitWidth)
    {
    if (BitWidth > MaxBits) BOOL_ALGEBRA_ERROR() ;
    this -> BitWidth = BitWidth ;
    nBytes = (BitWidth + 7) >> 3 ;
    nWords = (BitWidth + BIT_MASK) >> BIT_SHIFT ;
    memset (d, 0, sizeof (d)) ;
    }

  void Read (FILE* fp, uint32_t BitWidth) ;
  void Write (FILE* fp) ;
  bool BitSet (uint32_t bitnum) const ;
  bool SetBit (uint32_t bitnum) // Returns previous state
    {
    if (bitnum >= BitWidth) BOOL_ALGEBRA_ERROR() ;
    bool Prev = BitSet (bitnum) ;
    d[bitnum >> BIT_SHIFT] |= ONE << (bitnum & BIT_MASK) ;
    return Prev ;
    }
  bool IsZero() const { return !memcmp (d, Zero,sizeof (d)) ; }

  BoolVector& operator= (const BoolVector& v) ;
  bool operator[] (std::size_t idx) const { return BitSet (idx) ; }
  bool operator== (const BoolVector& v) const
    {
    return memcmp (d, v.d, sizeof (d)) == 0 ;
    }
  bool operator!= (const BoolVector& v) const
    {
    return !operator== (v) ;
    }

  // Boolean operations
  BoolVector& operator+= (const BoolVector& v) ;   // boolean OR
  BoolVector& operator-= (const BoolVector& v) ;   // boolean AND NOT
  bool operator* (const BoolVector& v) const ; // boolean AND

  // a <= b means that every 1-bit in a is also set in b. Similarly for >=.
  // So it is not the case that for all a,b, a <= b || b <= a
  bool operator<= (const BoolVector& v) const ;
  bool operator>= (const BoolVector& v) const ;

  static void Multiply (BoolVector* U, const BoolVector* V, const BoolVector* W) ;
  static bool GreaterOrEqual (const BoolVector* U, const BoolVector* V) ;

  bitmap_t d[MaxWords] ;

  uint16_t BitWidth ;
  uint16_t nBytes ;
  uint16_t nWords ;

private:
  static bitmap_t Zero[MaxWords] ;
  } ;

template<int MaxBits> bitmap_t BoolVector<MaxBits>::Zero[MaxWords] ;

template <int MaxBits> class BoolMatrix
  {
public:
  static const int MaxBytes = (MaxBits + 7) >> 3 ;
  static const int MaxWords = (MaxBits + BIT_MASK) >> BIT_SHIFT ;

  explicit BoolMatrix (uint32_t BitWidth = 0)
    {
    SetBitWidth (BitWidth) ;
    }
  BoolMatrix (const BoolMatrix& M) : BoolMatrix (M.BitWidth)
    {
    if (&M == this) return ;
    SetBitWidth (BitWidth) ;
    for (uint32_t i = 0 ; i < BitWidth ; i++) A[i] = M[i] ;
    }

  void Read (FILE* fp, uint32_t BitWidth) ;
  void Write (FILE* fp) ;
  void SetBitWidth (uint32_t BitWidth)
    {
    this -> BitWidth = BitWidth ;
    for (uint32_t i = 0 ; i < BitWidth ; i++)
      A[i].SetBitWidth (BitWidth) ;
    }

  BoolMatrix& operator= (const BoolMatrix& v) ;
  BoolVector<MaxBits>& operator[] (std::size_t idx)
    {
    return A[idx] ;
    }
  const BoolVector<MaxBits>& operator[] (std::size_t idx) const
    {
    return A[idx] ;
    }

  bool operator== (const BoolMatrix& v) const
    {
    for (uint32_t i = 0 ; i < BitWidth ; i++)
      if (A[i] != v[i]) return false ;
    return true ;
    }
  bool operator!= (const BoolMatrix& v) const
    {
    return !operator== (v) ;
    }

  // A <= B means that every 1-bit in A is also set in B. Similarly for >=.
  // So it is not the case that for all A,B, A <= B || B <= A
  bool operator<= (const BoolMatrix& M) const ;
  bool operator>= (const BoolMatrix& M) const ;

  static void Multiply (BoolMatrix& U, const BoolMatrix& V, const BoolMatrix& W) ;

  uint16_t BitWidth ;

  BoolVector<MaxBits> A[MaxBits] ;
  } ;

// User-defined operators to post-/pre-multiply a BoolVector by a matrix
template<int MaxBits> BoolVector<MaxBits> operator*
  (const BoolVector<MaxBits>& v, const BoolMatrix<MaxBits>& M) ; // Compute v M
template<int MaxBits> BoolVector<MaxBits> operator*
  (const BoolMatrix<MaxBits>& M, const BoolVector<MaxBits>& v) ; // Compute M v

template<int MaxBits> void BoolVector<MaxBits>::Read (FILE* fp, uint32_t BitWidth)
  {
  SetBitWidth (BitWidth) ;
  if (fread (d, nBytes, 1, fp) != 1) BOOL_ALGEBRA_ERROR() ;
  }

template<int MaxBits> void BoolVector<MaxBits>::Write (FILE* fp)
  {
  if (fwrite (d, nBytes, 1, fp) != 1) BOOL_ALGEBRA_ERROR() ;
  }

template<int MaxBits> bool BoolVector<MaxBits>::BitSet (uint32_t n) const 
  {
  if (n >= BitWidth) BOOL_ALGEBRA_ERROR() ;
  return (d[n >> BIT_SHIFT] & (ONE << (n & BIT_MASK))) != 0 ;
  }

template<int MaxBits> BoolVector<MaxBits>& BoolVector<MaxBits>::operator= (const BoolVector<MaxBits>& v)
  {
  if (&v == this) return *this ;
  SetBitWidth (v.BitWidth) ;
  memcpy (d, v.d, nBytes) ;
  return *this ;
  }

template<int MaxBits> BoolVector<MaxBits>& BoolVector<MaxBits>::operator+= (const BoolVector<MaxBits>& v)
  {
  if (v.BitWidth != BitWidth) BOOL_ALGEBRA_ERROR() ;
  for (uint32_t i = 0 ; i < nWords ; i++)
    d[i] |= v.d[i] ;
  return *this ;
  }

template<int MaxBits> BoolVector<MaxBits>& BoolVector<MaxBits>::operator-= (const BoolVector<MaxBits>& v)
  {
  if (v.BitWidth != BitWidth) BOOL_ALGEBRA_ERROR() ;
  for (uint32_t i = 0 ; i < nWords ; i++)
    d[i] &= ~v.d[i] ;
  return *this ;
  }

template<int MaxBits> bool BoolVector<MaxBits>::operator* (const BoolVector<MaxBits>& v) const
  {
  if (v.BitWidth != BitWidth) BOOL_ALGEBRA_ERROR() ;
  for (uint32_t i = 0 ; i < nWords ; i++)
    if (d[i] & v.d[i]) return true ;
  return false ;
  }

template<int MaxBits> bool BoolVector<MaxBits>::operator<= (const BoolVector<MaxBits>& v) const
  {
  if (v.BitWidth != BitWidth) BOOL_ALGEBRA_ERROR() ;
  for (uint32_t i = 0 ; i < nWords ; i++)
    if (d[i] & ~v.d[i]) return false ;
  return true ;
  }

template<int MaxBits> bool BoolVector<MaxBits>::operator>= (const BoolVector<MaxBits>& v) const
  {
  if (v.BitWidth != BitWidth)
    BOOL_ALGEBRA_ERROR() ;
  for (uint32_t i = 0 ; i < nWords ; i++)
    if (~d[i] & v.d[i]) return false ;
  return true ;
  }

template<int MaxBits> BoolMatrix<MaxBits>& BoolMatrix<MaxBits>::operator= (const BoolMatrix<MaxBits>& v)
  {
  if (&v == this) return *this ;
  SetBitWidth (v.BitWidth) ;
  for (uint32_t i = 0 ; i < BitWidth ; i++)
    A[i] = v[i] ;
  return *this ;
  }

// BoolVector<MaxBits> operator* (const BoolVector<MaxBits>& v, const BoolMatrix<MaxBits>* M)
//
// Returns v * M

template<int MaxBits> BoolVector<MaxBits> operator* (const BoolVector<MaxBits>& v, const BoolMatrix<MaxBits>& M)
  {
  BoolVector<MaxBits> result (v.BitWidth) ;
  for (uint32_t i = 0 ; i < v.BitWidth ; i++)
    if (v[i]) result += M[i] ;
  return result ;
  }

// BoolVector<MaxBits> operator* (const BoolMatrix<MaxBits>& M, const BoolVector<MaxBits>& v)
//
// Returns M * v

template<int MaxBits> BoolVector<MaxBits> operator* (const BoolMatrix<MaxBits>& M, const BoolVector<MaxBits>& v)
  {
  BoolVector<MaxBits> result (v.BitWidth) ;
  for (uint32_t i = 0 ; i < v.BitWidth ; i++)
    if (v * M[i]) result.SetBit (i) ;
  return result ;
  }

template<int MaxBits> void BoolMatrix<MaxBits>::Read (FILE* fp, uint32_t BitWidth)
  {
  SetBitWidth (BitWidth) ;
  for (uint32_t i = 0 ; i < BitWidth ; i++)
    A[i].Read (fp, BitWidth) ;
  }

template<int MaxBits> void BoolMatrix<MaxBits>::Write (FILE* fp)
  {
  for (uint32_t i = 0 ; i < BitWidth ; i++)
    A[i].Write (fp) ;
  }

// void BoolVector<MaxBits>::Multiply (BoolVector<MaxBits>* U, const BoolVector<MaxBits>* V, const BoolVector<MaxBits>* W)
//
// Matrix multiplication: U = V * W

template<int MaxBits> void BoolMatrix<MaxBits>::Multiply (BoolMatrix<MaxBits>& U,
  const BoolMatrix<MaxBits>& V, const BoolMatrix<MaxBits>& W)
  {
  for (uint32_t i = 0 ; i < V.BitWidth ; i++)
    U[i] = V[i] * W ;
  }

template<int MaxBits> bool BoolMatrix<MaxBits>::operator<= (const BoolMatrix<MaxBits>& M) const
  {
  if (M.BitWidth != BitWidth) BOOL_ALGEBRA_ERROR() ;
  for (uint32_t i = 0 ; i < BitWidth ; i++)
    if (!(A[i] <= M[i])) return false ;
  return true ;
  }

template<int MaxBits> bool BoolMatrix<MaxBits>::operator>= (const BoolMatrix<MaxBits>& M) const
  {
  if (M.BitWidth != BitWidth) BOOL_ALGEBRA_ERROR() ;
  for (uint32_t i = 0 ; i < BitWidth ; i++)
    if (!(A[i] >= M[i])) return false ;
  return true ;
  }
