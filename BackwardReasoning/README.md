To compile with g++ 12.2.0, run Compile.bat.
To generate umf and dvf files, run Run.bat.

With parameters -T1000 -S200, this Decider takes the 77,434,826 undecided machines from the Cyclers Decider and classifies 37,090,723 machines as non-halting, leaving 40,344,103 undecided machines. Time: 671s.

Decider
-------
BackwardReasoning <param> <param>...
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
           -S<depth limit>        Max search depth

Verifier
--------
No Verifier is provided.

Verification File Format
------------------------
  uint nEntries
  VerificationEntry[nEntries]

  VerificationEntry format:
    uint SeedDatabaseIndex
    uint DeciderType       -- 4 = BackwardReasoning
    uint InfoLength        -- 16 = length of decider-specific info
    int Leftmost           -- Leftmost tape head position
    int Rightmost          -- Rightmost tape head position
    uint MaxDepth
    uint nNodes


Unfortunately the BackwardReasoning machines are not amenable to verification in the same way as Cyclers and TranslatedCyclers. So acceptance is difficult to justify, as there seems to be no better method than simply analysing the source code for bugs...

