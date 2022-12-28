To compile with g++ 7.3.0, run Compile.bat.
To generate umf and dvf files, run Run.bat.

NOTE: TranslatedCyclers.umf is too big for me to commit to github. So you will have to generate your own using the TranslatedCyclers Decider.

This Decider takes the 3,574,222 undecided machines from the TranslatedCyclers Decider (with time limit 10,000) and classifies 2,035,598 machines as non-halting, leaving 1,538,624 undecided machines. Time: 21.7s.

My TranslatedCyclers Decider actually leaves 3,573,643 machines undecided, not 3,574,222; I used the bbchallenge.org list to facilitate comparisons with official results.

Unfortunately the BackwardReasoning machines are not amenable to verification in the same way as Cyclers and TranslatedCyclers. So acceptance is difficult to justify, as there seems to be no better method than simply analysing the source code for bugs...

The format of the Verification File BackwardReasoning.dvf:

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
