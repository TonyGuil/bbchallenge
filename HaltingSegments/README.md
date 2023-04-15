To compile with g++ 12.2.0, run Compile.bat.
To generate umf and dvf files, run Run.bat.

With parameter -W21, this Decider takes the 132,538 undecided machines from the Bouncers Decider and classifies 46,581 machines as non-halting, leaving 85,957 undecided machines. Time (limited to 4 threads): 2 hours.

Decider
-------
 HaltingSegments <param> <param>...
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
            -W<width limit>       Max segment width (must be odd)
            -S<stack depth>       Max stack depth (default 10000)

To compile with g++ 12.2.0, run Compile.bat.
To generate umf and dvf files, run Run.bat.

Verifier
--------
No Verifier is provided.

This Decider takes the 1,538,624 undecided machines from the BackwardReasoning Decider and classifies 1,022,908 machines as non-halting. Time: 61 minutes. Maximum segment width was set to 17.

Iijil's HaltingSegments Decider from the bbchallenge site classified 995,554 machines as halting. All of these machines are classified as non-halting by my Decider. Some of the 27,354 new machines are due to the deeper search, but most of them are due to my segment-matching criterion: a segment is found to *satisfy* a previously examined segment (thus making deeper search unnecessary) if the state is the same, and the current tape contents are compatible with the previous contents, perhaps shifted left or right. So for example the segment
```
D:  . 1 0 1 1 [0] 0 1 . .
```
satisfies the previous segment
```
D:  . . . . 0 1 1 [0] 0 .
```
because it matches all the *determined* tape cells in the previous segment, albeit shifted left by 2.

The current format of the Verification File HaltingSegments.dvf is:
```
  uint nEntries
  VerificationEntry[nEntries]

  VerificationEntry format:
    uint SeedDatabaseIndex
    uint DeciderType       -- 5 = HaltingSegments
    uint InfoLength        -- 20 = length of decider-specific info
    int Leftmost           -- Leftmost tape head position
    int Rightmost          -- Rightmost tape head position
    uint MaxDepth          -- Max search depth
    uint nNodes            -- Max number of nodes in the search tree
    uint MaxSegmentWidth   -- Must be an odd number
```
This is not much use as it stands. But the code has been modified to visit nodes in exactly the same order as Iijil's Decider (until an early cut-off is reached due to the wider segment-matching criterion). So I could modify the code further to emit such wider matches to the Verification File, indexed by node number. Then it would not be a great deal of work to modify Iijil's Decider to use this information to verify such nodes as it comes to them. The information would consist simply of the node index, and the earlier match that Iijil's Decider can look up in its `seenConfigurations` map. Suppose, for example, the next entry in the Verification Data is `{ 123, "-1C...011..." }`. At node 123, the machine configuration might be, for instance, `"-2C..01101.."`. Now it is easy to check whether (i) `"-1C...011..."` is in the `seenConfigurations` map and (ii) `"-2C..01101.."` satisfies `"-1C...011..."` (after shifting left by one).

However, see [my comment here](http://discuss.bbchallenge.org/t/decider-finite-automata-reduction/123/7?u=tonyg): this approach, while facilitating the acceptance of the extra machines, would be redundant if @UncombedCoconut’s results with Finite Automata Reduction are correct. So I will only implement this if there is a call for it.
