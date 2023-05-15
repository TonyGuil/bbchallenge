// bbchallenge.h
//
// Header file for Busy Beaver Challenge code

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <bit>

// ustring and ustring_view are std::string and std::string_view for unsigned chars
typedef std::basic_string<uint8_t> ustring ;
typedef std::basic_string_view<uint8_t> ustring_view ;

#define MAX_SPACE          12289
#define MAX_MACHINE_STATES 6
#define TAPE_SENTINEL      2

// Number of machines of each type in the Seed Database:
#define NTIME_LIMITED  14322029
#define NSPACE_LIMITED 74342035

#define VERIF_HEADER_LENGTH 12 // Verification data header length

#define TM_ERROR() \
  printf ("\n#%d: Error at line %d in %s\n", \
  SeedDatabaseIndex, __LINE__, __FUNCTION__), exit (1)

// For constant-length verification data, define VERIF_INFO_LENGTH
// in the cpp file, and then you can use VERIF_ENTRY_LENGTH:
#define VERIF_ENTRY_LENGTH (VERIF_HEADER_LENGTH + (VERIF_INFO_LENGTH))

enum class DeciderTag : uint32_t
  {
  NONE                    = 0,
  CYCLER                  = 1,
  TRANSLATED_CYCLER_RIGHT = 2,
  TRANSLATED_CYCLER_LEFT  = 3,
  BACKWARD_REASONING      = 4,
  HALTING_SEGMENT         = 5,
  BOUNCER                 = 6,
  NEW_BOUNCER             = 7,

  // Finite Automata Reduction variants
  FAR_DFA_ONLY            = 10,
  FAR_DFA_NFA             = 11,

  HALT                    = 100,
  } ;

enum class StepResult : uint8_t
  {
  OK,
  HALT,
  OUT_OF_BOUNDS
  } ;

//
// Endianness (data files are big-endian, platform may be big- or little-endian)
//

inline uint32_t Load32 (const void* p)
  {
  return (std::endian::native == std::endian::little)
    ? __builtin_bswap32 (*(uint32_t*)p)
    : *(uint32_t*)p ;
  }

inline uint16_t Load16 (const void* p)
  {
  return (std::endian::native == std::endian::little)
    ? __builtin_bswap16 (*(uint16_t*)p)
    : *(uint16_t*)p ;
  }

inline uint32_t Save32 (void* p, uint32_t n)
  {
  if constexpr (std::endian::native == std::endian::little) n = __builtin_bswap32 (n) ;
  *(uint32_t*)p = n ;
  return 4 ;
  }

inline uint32_t Save16 (void* p, uint16_t n)
  {
  if constexpr (std::endian::native == std::endian::little) n = __builtin_bswap16 (n) ;
  *(uint16_t*)p = n ;
  return 2 ;
  }

// inline uint32_t Read (FILE* fp, void* p, size_t len)
//
// inline uint32_t Read32 (FILE* fp)
// inline uint32_t Read16u (FILE* fp)
// inline int32_t Read16s (FILE* fp)
// inline uint32_t Read8u (FILE* fp)
// inline int32_t Read8s (FILE* fp)
//
// Read signed or unsigned big-endian integers of various sizes from a file,
// reversing the bytes if necessary

inline void Read (FILE* fp, void* p, size_t len)
  {
  if (len && fread (p, len, 1, fp) != 1) printf ("\nRead error\n"), exit (1) ;
  }

inline uint32_t Read32 (FILE* fp)
  {
  uint32_t t ; Read (fp, &t, 4) ;
  if constexpr (std::endian::native == std::endian::little) t = __builtin_bswap32 (t) ;
  return t ;
  }

inline uint32_t Read16u (FILE* fp)
  {
  uint16_t t ; Read (fp, &t, 2) ;
  if constexpr (std::endian::native == std::endian::little) t = __builtin_bswap16 (t) ;
  return t ;
  }

inline int32_t Read16s (FILE* fp)
  {
  int16_t t ; Read (fp, &t, 2) ;
  if (std::endian::native == std::endian::little) t = __builtin_bswap16 (t) ;
  return t ;
  }

inline uint32_t Read8u (FILE* fp)
  {
  uint8_t t ; Read (fp, &t, 1) ;
  return t ;
  }

inline int32_t Read8s (FILE* fp)
  {
  int8_t t ; Read (fp, &t, 1) ;
  return t ;
  }

inline bool CheckEndOfFile (FILE* fp)
  {
  // Try and read a byte
  uint8_t t ;
  return fread (&t, 1, 1, fp) == 0 ;
  }

// inline void Write (FILE* fp, const void* p, size_t len)
//
// inline void Write32 (FILE* fp, uint32_t n)
// inline void Write16 (FILE* fp, uint32_t n)
// inline void Write8 (FILE* fp, uint32_t n)
//
// Write a big-endian integer to a file, reversing the bytes if necessary

inline void Write (FILE* fp, const void* p, size_t len)
  {
  if (fp && len && fwrite (p, len, 1, fp) != 1)
    printf ("Write error\n"), exit (1) ;
  }

inline void Write32 (FILE* fp, uint32_t n)
  {
  if (std::endian::native == std::endian::little) n = __builtin_bswap32 (n) ;
  Write (fp, &n, 4) ;
  }

inline void Write16 (FILE* fp, uint16_t n)
  {
  if (std::endian::native == std::endian::little) n = __builtin_bswap16 (n) ;
  Write (fp, &n, 2) ;
  }

inline void Write8 (FILE* fp, uint8_t n)
  {
  Write (fp, &n, 1) ;
  }
