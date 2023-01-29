Decider `DecideFAR.exe` and Verifier `VerifyFAR.exe` for Finite Automata Reduction.

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
  ushort NFA_States -- Should equal 5*DFA_States + 1
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
`VerifyFAR.exe` verifies whichever format it finds; if run with `-F`, it also generates the `NFA` from the `DFA` and compares it with the `NFA` from the dvf:
```
  VerifyFAR <param> <param>...
    <param>: -D<database>           Seed database file (defaults to ../SeedDatabase.bin)
             -V<verification file>  Decider Verification File for decided machines
             -X<test machine>       Machine to test
             -F                     Reconstruct NFA and check it against NFA in dvf
             -O                     Trace output
```
