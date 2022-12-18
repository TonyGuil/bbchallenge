NOTE: These programs expect SeedDatabase.bin in the root directory. This file is too big for me to publish on github, so you will need to provide your own. It is simply the original seed database file from the bbchallenge site, with a 30-byte header and a 30-byte entry for each machine.

Currently implemented deciders:
  Cyclers
  TranslatedCyclers
  BackwardReasoning
  HaltingSegments
  Bouncers

These deciders take as input an Undecided Machine File (umf) from the previous decider and generate a (hopefully shorter) umf file as output, together with a Decider Verification File (dvf) that can be used by a Verifier to verify that all the decided machines are correct.

File Formats
============
Unless otherwise noted, all fields are 32-bit big-endian integers, signed (int) or unsigned (uint).

Format of Undecided Machine File
--------------------------------
  The seed dabatase index of each undecided file, in increasing order

Format of Decider Verification File
-----------------------------------

  uint nEntries
  VerificationEntry[nEntries]

  VerificationEntry format:
    uint SeedDatabaseIndex
    uint DeciderType -- 1 = Cyclers
                     -- 2 = TranslatedCyclers (translated to the right)
                     -- 3 = TranslatedCyclers (translated to the left)
                     -- 4 = BackwardReasoning
                     -- 5 = HaltingSegments
                     -- 6 = Bouncers
    uint InfoLength  -- Length of decider-specific info for this machine
    byte  DeciderSpecificInfo[InfoLength]

Format of Decider-specific info
-------------------------------

[Decider] Cyclers
  -- An initial configuration matches a final configuration (the state, tape head,
  -- and tape contents match).
  -- Leftmost and Rightmost are for the convenience of the Verifier, and not strictly necessary.
  int Leftmost          -- Leftmost tape head position
  int Rightmost         -- Rightmost tape head position
  uint State            -- State of machine in initial and final configurations
  int TapeHead          -- Tape head of machine in initial and final configurations
  uint InitialStepCount -- Number of steps to reach initial configuration
  uint FinalStepCount   -- Number of steps to reach final configuration

[Decider] TranslatedCyclers
  -- An initial configuration matches a final configuration translated left or right.
  -- Leftmost and Rightmost are for the convenience of the Verifier, and not strictly necessary.
  int Leftmost            -- Leftmost tape head position
  int Rightmost           -- Rightmost tape head position
  uint State              -- State of machine in initial and final configurations
  int InitialTapeHead     -- Tape head in initial configuration
  int FinalTapeHead       -- Tape head in final configuration
  uint InitialStepCount   -- Number of steps to reach initial configuration
  uint FinalStepCount     -- Number of steps to reach final configuration
  uint MatchLength        -- Length of match

[Decider] BackwardReasoning
[Decider] HaltingSegments
  These two deciders currently have no useful Verification info.

[Decider] Bouncers
  See https://github.com/TonyGuil/bbchallenge/tree/main/Bouncers#readme for a full description.