// DecideFAR <param> <param>...
//   <param>: -N<states>            Machine states (2, 3, 4, 5, or 6)
//            -D<database>          Seed database file (defaults to ../SeedDatabase.bin)
//            -V<verification file> Output file: verification data for decided machines
//            -I<input file>        Input file: list of machines to be analysed (default=all machines)
//            -U<undecided file>    Output file: remaining undecided machines
//            -X<test machine>      Machine to test
//            -M<machine spec>      Compact machine code (ASCII spec) to test
//            -L<machine limit>     Max no. of machines to test
//            -H<threads>           Number of threads to use
//            -O                    Print trace output
//            -A<DFA states>        Number of DFA states
//            -F                    Output NFA to dvf as well as DFA

#include <ctype.h>
#include <string>
#include <vector>
#include <mutex>

#include "FAR.h"
#include "../Params.h"

#if NEED_BOOST_THREADS
  #include <boost/thread.hpp>
  using boost::thread ;
#else
  #include <thread>
  using std::thread ;
#endif

std::mutex RangeMutex ;
static uint32_t TotalAssigned = 0 ;
static uint32_t TotalCompleted = 0 ;
static uint32_t TotalDecided = 0 ;
static uint32_t LastPercent = -1 ;

static uint32_t* MachineIndexList ;
static uint8_t* MachineSpecList ;
static uint8_t* VerificationList ;

class CommandLineParams : public DeciderParams
  {
public:
  uint32_t DFA_States ;
  bool DFA_StatesPresent = false ;
  bool OutputNFA ;
  void Parse (int argc, char** argv) ;
  void PrintHelpAndExit [[noreturn]] (int status) ;
  } ;

static CommandLineParams Params ;
static TuringMachineReader Reader ;

static void ThreadFunction() ;
struct Range
  {
  uint32_t First ;
  uint32_t Last ; // not included in the range
  } ;
static bool GetNextRange (Range& R, uint32_t nCompleted, uint32_t nDecided) ;

int main (int argc, char** argv)
  {
  Params.Parse (argc, argv) ;
  Params.CheckParameters() ;
  Params.OpenFiles() ;

  Reader.SetParams (&Params) ;

  if (!Params.nThreadsPresent)
    {
    if (Reader.SingleEntry) Params.nThreads = 1 ;
    else
      {
      Params.nThreads = 4 ;
      char* env = getenv ("NUMBER_OF_PROCESSORS") ;
      if (env)
        {
        Params.nThreads = atoi (env) ;
        if (Params.nThreads == 0) Params.nThreads = 4 ;
        }
      printf ("nThreads = %d\n", Params.nThreads) ;
      }
    }

  clock_t Timer = clock() ;

  // Read all the machines into memory
  MachineIndexList = new uint32_t[Reader.nMachines] ;
  MachineSpecList = new uint8_t[Reader.nMachines * Reader.MachineSpecSize] ;
  VerificationList = new uint8_t[Reader.nMachines * (1 + 2 * Params.DFA_States)] ;
  for (uint32_t i = 0 ; i < Reader.nMachines ; i++)
    MachineIndexList[i] = Reader.Next (MachineSpecList + i * Reader.MachineSpecSize) ;

  // Run inline if single thread (for ease of debugging)
  if (Params.nThreads == 1) ThreadFunction() ;
  else
    {
    std::vector<thread*> ThreadList (Params.nThreads) ;
    for (uint32_t i = 0 ; i < Params.nThreads ; i++)
      ThreadList[i] = new thread (ThreadFunction) ;
    for (uint32_t i = 0 ; i < Params.nThreads ; i++)
      {
      ThreadList[i] -> join() ; // Wait for thread i to finish
      delete ThreadList[i] ;
      }
    }

  printf ("\r100%% %d %d\n", Reader.nMachines, TotalDecided) ;
  fflush (stdout) ;

  if (Params.fpInput) fclose (Params.fpInput) ;

  if (Params.fpVerify || Params.fpUndecided)
    {
    Write32 (Params.fpVerify, TotalDecided) ;

    const uint32_t* IndexList = MachineIndexList ;
    const uint8_t* SpecList = MachineSpecList ;
    uint8_t* VerifList = VerificationList ;
    for (uint32_t i = 0 ; i < Reader.nMachines ; i++)
      {
      if (VerifList[0] == 0xFF) Write32 (Params.fpUndecided, *IndexList) ; // Undecided
      else if (Params.fpVerify)
        {
        Write32 (Params.fpVerify, *IndexList) ;
        if (Params.OutputNFA)
          {
          Write32 (Params.fpVerify, (uint32_t)DeciderTag::FAR_DFA_NFA) ;

          // Write DFA and NFA, with some header information
          FiniteAutomataReduction Decider (Params.MachineStates, nullptr, true) ;
          Decider.SetDFA_States (Params.DFA_States) ;
          Decider.Direction = VerifList[0] ;
          memcpy (Decider.DFA, VerifList + 1, 2 * Decider.DFA_States) ;

          uint32_t nBytes = (Decider.NFA_States + 7) >> 3 ;
          Write32 (Params.fpVerify, 5 + 2 * Decider.DFA_States + (2 * Decider.NFA_States + 1) * nBytes) ;
          Write8 (Params.fpVerify, VerifList[0]) ; // Direction
          Write16 (Params.fpVerify, Decider.DFA_States) ;
          Write16 (Params.fpVerify, Decider.NFA_States) ;

          // Write DFA
          Write (Params.fpVerify, VerifList + 1, 2 * Decider.DFA_States) ;

          // Reconstruct NFA from DFA
          Decider.ReconstructNFA (SpecList) ;

          // Write NFA
          for (uint32_t r = 0 ; r <= 1 ; r++)
            for (uint32_t i = 0 ; i < Decider.NFA_States ; i++)
              Write (Params.fpVerify, Decider.R[r][i].d, nBytes) ;

          // Write a
          Write (Params.fpVerify, Decider.a.d, nBytes) ;
          }
        else
          {
          // Write DFA only
          Write32 (Params.fpVerify, (uint32_t)DeciderTag::FAR_DFA_ONLY) ;
          Write32 (Params.fpVerify, 1 + 2 * Params.DFA_States) ;
          Write (Params.fpVerify, VerifList, 1 + 2 * Params.DFA_States) ;
          }
        }
      IndexList++ ;
      SpecList += Reader.MachineSpecSize ;
      VerifList += 1 + 2 * Params.DFA_States ;
      }
    if (Params.fpVerify) fclose (Params.fpVerify) ;
    if (Params.fpUndecided) fclose (Params.fpUndecided) ;
    }

  Timer = clock() - Timer ;

  printf ("\nDecided %d out of %d\n", TotalDecided, Reader.nMachines) ;
  printf ("Elapsed time %.3f\n", (double)Timer / CLOCKS_PER_SEC) ;
  }

static void ThreadFunction()
  {
  FiniteAutomataReduction Decider (Params.MachineStates, nullptr, false, Params.TraceOutput) ;

  uint32_t nDecided = 0 ;
  uint32_t nCompleted = 0 ;
  for ( ; ; )
    {
    Range R ;
    if (!GetNextRange (R, nCompleted, nDecided)) return ;
    const uint32_t* IndexList = MachineIndexList + R.First ;
    const uint8_t* SpecList = MachineSpecList + R.First * Reader.MachineSpecSize ;
    uint8_t* VerifList = VerificationList + R.First * (1 + 2 * Params.DFA_States) ;
    nDecided = nCompleted = 0 ;
    for (uint32_t i = R.First ; i < R.Last ; i++, nCompleted++)
      {
      if (Decider.RunDecider (Params.DFA_States, SpecList, VerifList)) nDecided++ ;
      IndexList++ ;
      SpecList += Reader.MachineSpecSize ;
      VerifList += 1 + 2 * Params.DFA_States ;
      }
    }
  }

static bool GetNextRange (Range& R, uint32_t nCompleted, uint32_t nDecided)
  {
  // Get exclusive access to Range variables
  std::lock_guard<std::mutex> MutexLock { RangeMutex } ;

  TotalDecided += nDecided ;
  TotalCompleted += nCompleted ;
  uint32_t Percent = (TotalCompleted * 100LL) / Reader.nMachines ;
  if (Percent != LastPercent)
    {
    LastPercent = Percent ;
    printf ("\r%d%% %d %d", Percent, TotalCompleted, TotalDecided) ;
    fflush (stdout) ;
    }

  if (TotalAssigned == Reader.nMachines) return false ;
  uint32_t nRemaining = Reader.nMachines - TotalAssigned ;

  // Try to ensure that we are not left waiting for a single thread to complete
  // while everybody else has long since finished, by reducing the chunk size
  // as we near the end (this expression is plucked out of nowhere, but it's
  // not very important):
  uint32_t ChunkSize = nRemaining / (5 * Params.nThreads) + 1 ;
  if (ChunkSize * Params.nThreads * 50 > Reader.nMachines)
    {
    ChunkSize = Reader.nMachines / (Params.nThreads * 50) ;
    if (ChunkSize == 0) ChunkSize = 1 ;
    }
  if (TotalAssigned + ChunkSize > Reader.nMachines) ChunkSize = Reader.nMachines - TotalAssigned ;

  R.First = TotalAssigned ;
  R.Last = R.First + ChunkSize ;
  TotalAssigned += ChunkSize ;
  return true ;
  }

void CommandLineParams::Parse (int argc, char** argv)
  {
  if (argc == 1) PrintHelpAndExit (0) ;

  for (argc--, argv++ ; argc ; argc--, argv++)
    {
    if (DeciderParams::ParseParam (argv[0])) continue ;
    if (argv[0][0] != '-') printf ("Invalid parameter \"%s\"\n", argv[0]), PrintHelpAndExit (1) ;
    switch (toupper (argv[0][1]))
      {
      case 'A':
        DFA_States = atoi (&argv[0][2]) ;
        DFA_StatesPresent = true ;
        if (DFA_States > FiniteAutomataReduction::MaxDFA_States)
          printf ("DFA_States too large (max %d)\n", FiniteAutomataReduction::MaxDFA_States), exit (1) ;
        break ;

      case 'F':
        OutputNFA = true ;
        break ;

      default:
        printf ("Invalid parameter \"%s\"\n", argv[0]) ;
        PrintHelpAndExit (1) ;
      }
    }

  if (!DFA_StatesPresent) printf ("DFA states not specified\n"), PrintHelpAndExit (1) ;
  }

void CommandLineParams::PrintHelpAndExit (int status)
  {
  printf ("DecideFAR <param> <param>...") ;
  DeciderParams::PrintHelp() ;
  printf (R"*RAW*(
           -A<DFA states>        Number of DFA states
           -F                    Output NFA to dvf as well as DFA
)*RAW*") ;
  exit (status) ;
  }
