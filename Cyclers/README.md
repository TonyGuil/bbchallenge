To compile with g++ 12.2.0, run Compile.bat.
To generate umf and dvf files, run Run.bat.

With parameters -T1000 -S200, this Decider takes the 88,664,064 machines from the seed database and classifies 11,229,238 machines as non-halting, leaving 77,434,826 undecided machines. Time: 70s.

The Verifier verifies these 11,229,238 machines in a time of 52s.

Decider
-------
DecideCyclers <param> <param>...
  <param>: -N<states>            Machine states (5 or 6)
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
           -S<space limit>       Max absolute value of tape head)*RAW*") ;

Verifier
--------
VerifyCyclers <param> <param>...
  <param>: -N<states>            Machine states (5 or 6)
           -D<database>          Seed database file (defaults to ../SeedDatabase.bin)
           -V<verification file> Input file: verification data to be checked

Format of Cyclers Verification File
-----------------------------------
  uint nEntries
  VerificationEntry[nEntries]

  VerificationEntry format:
    uint SeedDatabaseIndex
    uint DeciderType      -- 1 = Cyclers
    uint InfoLength       -- 24 = length of decider-specific info
    int Leftmost          -- Leftmost tape head position
    int Rightmost         -- Rightmost tape head position
    uint State            -- State of machine in initial and final configurations
    int TapeHead          -- Tape head of machine in initial and final configurations
    uint InitialStepCount -- Number of steps to reach initial configuration
    uint FinalStepCount   -- Number of steps to reach final configuration
