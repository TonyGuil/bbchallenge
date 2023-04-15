To compile with g++ 12.2.0, run Compile.bat.<br>
To generate umf and dvf files, run Run.bat.

With parameter -A7, this Decider takes the 85,957 undecided machines from the Halting Segments Decider and classifies 84,370 machines as non-halting, leaving 1,587 undecided machines. Time (limited to 4 threads): 5.5 hours.

The Verifier verifies these 1,587 machines in a time of 5s.

Decider
-------
```
DecideFAR <param> <param>...
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
           -A<DFA states>        Number of DFA states
           -F                    Output NFA to dvf as well as DFA
```
Verifier
--------
```
VerifyFAR <param> <param>...
  <param>: -N<states>            Machine states (2, 3, 4, 5, or 6)
           -D<database>          Seed database file (defaults to ../SeedDatabase.bin)
           -V<verification file> Input file: verification data to be checked
           -F                    Reconstruct NFA and check it against NFA in dvf
```
Two dvf formats are supported:

- `DeciderTag = FAR_DFA_ONLY (10)`

Verification Entry format:
```
  ubyte Direction -- 0 is left-to-right, 1 is right-to-left
  ubyte DFA[DFA_States][2] -- DFA_States is deduced from the VerificationInfo length
```

- `DeciderTag = FAR_DFA_NFA (11)`

Verification Entry format:
```
  ubyte Direction -- 0 is left-to-right, 1 is right-to-left
  ushort DFA_States
  ushort NFA_States -- Should equal MachineStates*DFA_States + 1
  ubyte DFA[DFA_States][2]
  BoolVector NFA[2][NFA_States]
  BoolVector a
```
where `BoolVector` is a little-endian bitmap of `((NFA_States + 7) >> 3)` bytes.

`DecideFAR.exe` generates `FAR_DFA_ONLY` data, unless run with `-F`:
```
  DecideFAR <param> <param>...
    <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
             -N<DFA-states>         Number of DFA states
             -I<input file>         Input file: list of machines to be analysed
             -V<verification file>  Output file: verification data for decided machines
             -U<undecided file>     Output file: remaining undecided machines
             -X<test machine>       Machine to test
             -M<threads>            Number of threads to use
             -L<machine limit>      Max no. of machines to test
             -F                     Output NFA to dvf as well as DFA
             -O                     Print trace output
```
`VerifyFAR.exe` verifies whichever format it finds; if run with `-F`, it also generates the `NFA` from the `DFA` and compares it with the `NFA` from the dvf if present:
```
  VerifyFAR <param> <param>...
    <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
             -V<verification file>  Decider Verification File for decided machines
             -X<test machine>       Machine to test
             -F                     Reconstruct NFA and check it against NFA in dvf
             -O                     Trace output
```
