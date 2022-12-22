UncombedCoconut [reports](https://discuss.bbchallenge.org/t/decider-finite-automata-reduction/123/6) a provisional result of 4232 undecided machines after various runs of his Finite Automaton Reduction Decider. I have run these machines through my deciders:

- Two of them ([#31357173](https://bbchallenge.org/31357173) and [#45615747](https://bbchallenge.org/45615747)) are TranslatedCyclers! Verification file is TranslatedCyclers_Holdouts.dvf.

- 3543 of the remaining 4230 machines are Bouncers. Verification file is Bouncers_Holdouts.dvf.

The remaining 687 machines are in Bouncers_Holdouts.umf. Of these, 58 appear to be Bells (see ProbableBells.*).

If I run all 687 machines for 47,176,870 steps, the absolute leftmost and rightmost tape head positions are -17,814 ([#47561869](https://bbchallenge.org/47561869)) and 18,547 ([#59283023](https://bbchallenge.org/59283023)). #47561869 is interesting: it looks like a partitioned Bouncer, but the number of partitions grows and grows ([#46540896](https://bbchallenge.org/46540896) seems to be another example of this type). #59283023 is a Probable Bell.