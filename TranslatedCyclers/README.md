To compile with g++ 7.3.0, run Compile.bat.
To generate umf and dvf files, run Run.bat.

With a time limit of 20,000,000 steps and a space limit of 100,000 cells plus or minus, this Decider takes the 77,434,826 machines undecided by the Cyclers Decider and classifies 73,861,183 machines as non-halting. This is an increase of 579 over the 73,860,604 machines in the official database. 3,573,643 machines remain undecided. Time taken was 12 hours.

The Verifier verifies these 73,861,183 machines in a time of 13 minutes.

The dvf and umf files are too big for GitHub, but I ran the Decider on the machines left undecided in the official database; the verification file for the resulting 579 machines is TC_20000000.dvf.

The Decider found exactly one TranslatedDecider between 10,000,000 and 20,000,000 steps, so the returns are diminishing. But there are definitely longer TranslatedCyclers out there. For one thing, it looks like more machines would be found if the hard-coded parameter BACKWARD_SCAN_LENGTH=2000 was increased. I will try this when my laptop has an idle 12 hours again.

Format of TranslatedCyclers Verification File
---------------------------------------------
```
  uint nEntries
  VerificationEntry[nEntries]

  VerificationEntry format:
    uint SeedDatabaseIndex
    uint DeciderType       -- 2 = TranslatedCyclers (translated to the right)
                           -- 3 = TranslatedCyclers (translated to the left)
    uint InfoLength        -- 32 = length of decider-specific info for this machine
    int Leftmost           -- Leftmost tape head position
    int Rightmost          -- Rightmost tape head position
    uint State             -- State of machine in initial and final configurations
    int InitialTapeHead    -- Tape head in initial configuration
    int FinalTapeHead      -- Tape head in final configuration
    uint InitialStepCount  -- Number of steps to reach initial configuration
    uint FinalStepCount    -- Number of steps to reach final configuration
    uint MatchLength       -- Length of match
```
