Each of the six sub-directories contains a Decider; four of them contain an associated Verifier. All these programs have been converted to handle Turing machines with anything from 2 to 6 states. They have also been converted to 64-bit C++20, obviating the need for the Boost library.

Currently implemented Deciders:

-  Cyclers
-  TranslatedCyclers
-  BackwardReasoning
-  HaltingSegments
-  Bouncers
-  FAR (Finite Automata Reduction)

Currently implemented Verifiers:

-  Cyclers
-  TranslatedCyclers
-  Bouncers
-  FAR (Finite Automata Reduction)

All six deciders were run on the 5-state Seed Database from bbchallenge.org; see Results.txt for the results.

Decider Parameters
==================
All Deciders share a uniform command-line syntax, plus possible additional parameters which are documented in the relevant sub-directory:

Summary
-------
```
-N<states>            Machine states (2, 3, 4, 5, or 6)
-M<machine spec>      Compact machine code (ASCII spec) to test
-D<database>          Seed database file (defaults to ../SeedDatabase.bin)
-I<input file>        Input file: list of machines to be analysed (default=all machines)
-X<test machine>      Machine to test
-L<machine limit>     Max no. of machines to test
-V<verification file> Output file: verification data for decided machines
-U<undecided file>    Output file: remaining undecided machines
-H<threads>           Number of threads to use
-O                    Print trace output
```

Machine States
--------------
`-N<states>`<br>
Number of Turing Machine states -- an integer between 2 and 6 inclusive. The default is 5.

Machines to be Tested
---------------------
There are various options for specifying the set of machines to be tested. To specify a single machine with a given specification:<br>
`-M<machine spec>`<br>
`<machine spec>` is compact machine code in ASCII. For instance:<br>
`-M1RB1LE_1LC---_0RE1LD_0LB0RA_1LC0RF_1RE1RF`

Otherwise a seed database file is required. This can be specified with:<br>
`-D<database>`<br>
If unspecified, it defaults to ../SeedDatabase.bin.

If nstates is equal to 5, the seed database fle is assumed to consist of a 30-byte header followed by 30 binary bytes for each machine, as in the original seed database file from bbchallenge.org. Otherwise it consists of a list of compact machine code specs in ASCII format, with each line containing a compact spec. Lines must be separated by a single delimiter byte (which can take any value).

If an input file is specified with<br>
`-I<input file>`<br>
then the file contains a list of 4-byte big-endian integers in binary format; each index is the 0-based index of a machine in the seed database file. If no input file is specified, then all the machines in the seed database file are tested.

To specify a single machine for testing:<br>
`-X<test machine>`<br>
Again, <test machine> is the 0-based index of a machine in the seed database file.

To limit the total number of of machines tested:<br>
`-L<machine limit>`

Output Files
------------
To generate a Decider Verification File, or dvf:<br>
`-V<verification file>`<br>
This file contains a Verification Entry for each machine successfully decided as non-halting. The format of this file is described below.

To generate a list of machines that the Decider failed to categorise as non-halting (the Undecided Machines File, or umf):<br>
`-U<undecided file>`<br>
This file has the same format as the input file, so it can be used to chain Deciders by specifying the output of one as the input file of the next.

Miscellaneous
-------------
The Deciders will normally use as many threads as are available on the computer. To override this:<br>
`-H<threads>`

To generate diagnostic output (this output changes constantly during development, but is now largely non-existent):<br>
`-O`

Verifier Parameters
===================
Summary
-------
```
-N<states>            Machine states (2, 3, 4, 5, or 6)
-D<database>          Seed database file (defaults to ../SeedDatabase.bin)
-V<verification file> Output file: verification data for decided machines
```

Machine States
--------------
`-N<states>`<br>
Number of Turing Machine states -- an integer between 2 and 6 inclusive. The default is 5.

Seed database
-------------
To specify a seed database file:<br>
`-D<database>`<br>
If absent, it defaults to ../SeedDatabase.bin.

Verification File
-----------------
`-V<verification file>`<br>
This file contains a Verification Entry for each machine that has been categorised as non-halting.

File Formats
============
Unless otherwise noted, all fields are 32-bit big-endian integers, signed (int) or unsigned (uint).

Format of Undecided Machine File
--------------------------------
The seed dabatase index of each undecided file, in increasing order

Format of Decider Verification File
-----------------------------------
```
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
```
See the individual Decider documentation for the format of the DeciderSpecificInfo.
<br>