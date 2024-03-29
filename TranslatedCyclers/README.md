To compile with g++ 12.2.0, run Compile.bat.<br>
To generate umf and dvf files, run Run.bat.

With parameters -T20000000 -S100000, this Decider takes the 40,344,103 undecided machines from the Backward Reasoning Decider and classifies 38,805,555 machines as non-halting, leaving 1,538,548 undecided machines. Time (limited to 4 threads): 6 hours.

The Verifier verifies these 38,805,555 machines in a time of 249s.

Decider
-------
```
DecideTranslatedCyclers <param> <param>...
  <param>: -N<states>            Machine states (2, 3, 4, 5, or 6)
           -D<database>          Seed database file (defaults to ../SeedDatabase.bin)
           -V<verification file> Output file: verification data for decided machines
           -I<input file>        Input file: list of machines to be analysed (default=all machines)
           -U<undecided file>    Output file: remaining undecided machines
           -X<test machine>      Machine to test
           -M<machine spec>      Compact machine code (ASCII spec) to test
           -L<machine limit>     Max no. of machines to test
           -H<threads>           Number of threads to use
           -O                    Print trace output
           -T<time limit>        Max no. of steps
           -S<space limit>       Max absolute value of tape head
```
Verifier
--------
```
VerifyTranslatedCyclers <param> <param>...
  <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
           -V<verification file>  Input file: verification data to be checked
           -S<space limit>        Max absolute value of tape head
```
Format of Verification File
---------------------------
An initial configuration matches a final configuration translated left or right.
Leftmost and Rightmost are for the convenience of the Verifier, and not strictly necessary.
```
uint nEntries
VerificationEntry[nEntries]

VerificationEntry format:
  uint SeedDatabaseIndex
  uint DeciderType      -- 1 = Cyclers
  uint InfoLength       -- 24 = length of decider-specific info
  int Leftmost            -- Leftmost tape head position
  int Rightmost           -- Rightmost tape head position
  uint State              -- State of machine in initial and final configurations
  int InitialTapeHead     -- Tape head in initial configuration
  int FinalTapeHead       -- Tape head in final configuration
  uint InitialStepCount   -- Number of steps to reach initial configuration
  uint FinalStepCount     -- Number of steps to reach final configuration
  uint MatchLength        -- Length of match
```

With a time limit of 20,000,000 steps and a space limit of 100,000 cells plus or minus, this Decider takes the 77,434,826 machines undecided by the Cyclers Decider and classifies 73,861,183 machines as non-halting. This is an increase of 579 over the 73,860,604 machines in the official database. 3,573,643 machines remain undecided. Time taken was 12 hours.

The Verifier verifies these 73,861,183 machines in a time of 13 minutes.

The Decider found exactly one TranslatedDecider between 10,000,000 and 20,000,000 steps, so the returns are diminishing. But there are definitely longer TranslatedCyclers out there. For one thing, it looks like more machines would be found if the hard-coded parameter `BACKWARD_SCAN_LENGTH=5000` was increased. I will try this when my laptop has an idle 12 hours again.
