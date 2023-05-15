This is a Decider and Verifier for Bouncers.

To compile with g++ 12.2.0, run Compile.bat.<br>
To generate umf and dvf files, run Run.bat.

With parameters -T1000000 -S20000 -B, this Decider takes the 1,538,548 undecided machines from the Translate Cyclers Decider and classifies 1,406,010 machines as non-halting, leaving 132,538 undecided machines. Time (limited to 4 threads): 3.6 hours.

The Verifier verifies these 1,406,010 machines in a time of 36s.

The files ProbableBells.txt and ProbableBells.umf are text and binary dumps of the 18,651 probable bells found in the course of these runs.

Decider
-------
```DecideBouncers  <param> <param>...
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
           -T<time limit>        Max no. of steps
           -S<space limit>       Max absolute value of tape head
           -B[<bells-file>]      Output <bells-file>.txt and <bells-file>.umf (default ProbableBells)
```
Verifier
--------
```
VerifyBouncers <param> <param>...
  <param>: -N<states>            Machine states (2, 3, 4, 5, or 6)
           -D<database>          Seed database file (defaults to ../SeedDatabase.bin)
           -V<verification file> Input file: verification data to be checked
           -S<space limit>       Max absolute value of tape head
```
#### Introduction

A Bouncer of the simplest kind is a machine that bounces back and forth between two walls, which move further and further apart. These simple Bouncers can be categorised as:

- Unilateral Bouncer (e.g. [#4175994](https://bbchallenge.org/4175994)): one of the two wall remains stationary
- Bilateral Bouncer (e.g. [#8929416](https://bbchallenge.org/8929416)): both walls move, in different directions
- Translated Bouncer (e.g. [#20076854](https://bbchallenge.org/20076854)): both walls move in the same direction, while getting further apart

But Bouncers can be more complex. We define some terms:

- A *Run* is a sequence of operations (a *Repeater*) that is repeated a number of times,  followed by a sequence of operations (the *Wall*) that is executed once.
- A *Cycle* is a sequence of Runs that repeats indefinitely, with the number of Repeaters in each run increasing by one in each Cycle.

The Cycle for most Bouncers (about 84%) consists of two Runs. A further 14% consist of four runs (e.g. [#340](https://bbchallenge.org/340): notice how the state of the leftmost cell alternates between D and E). But some Bouncers consist of many Runs; the current record is [#3957107](https://bbchallenge.org/3957107), which executes 156 runs before repeating.

As well as the Multiple Bouncers, about 0.5% of Bouncers are what I call Partitioned Bouncers. These partition the tape into two or more regions, with a central wall that can be passed through or bounced off. Such Bouncers necessarily consist of at least four runs; [#29709](https://bbchallenge.org/29709) is a simple example. Bouncers with up to four partitions have been seen, for example [#43477769](https://bbchallenge.org/43477769).

This Decider finds all of these Bouncers.

#### Finding Bouncers

To find Bouncers, we run a machine for a number of steps (typically 100,000 or more), saving all Records as we encounter them (a Record is when the tape head reaches a cell that it has never visited before). If we find four Records with identical state whose tape heads are in arithmetic progression, but whose step counts are in quadratic progression, then we probably have a Bouncer (and if it's not a Bouncer, it's very probably a Bell -- see below).

Now we run the machine for two more Cycles, saving the state and tape head of each step. Then we compare these two Cycles, looking for repeated Runs where the second Cycle is identical to the first except for an extra Repeater. If we can successfully match up each run in the first Cycle with a corresponding run in the second Cycle, we very probably have a Bouncer.

When I say "probably", I mean that if it passes all the tests during the generation of the Verification Data, then it is a Bouncer; and if it passes enough of these tests, but not all, then it is very probably a Bell (e.g. [#73261028](https://bbchallenge.org/73261028)). A Bell is, loosely speaking, a sequence of Bouncers each of which gets interrupted in its journey to infinity and has to start again from scratch, growing exponentially the while. The DecideBouncers program has the option to output binary and text files containing the Probable Bells; no proof is provided, but such files will be useful for the development of any Bell Decider.

The program `DecideBouncers.exe` is compiled from:
- `DecideBouncers.cpp`, which handles command-line arguments, memory allocation, threads, and file I/O;
- `BouncerDecider.cpp`, which contains code used only by the Decider (NOTE: little or no effort has been made to render this code comprehensible. View it at your own risk);
- `Bouncer.cpp`, which contains code common to the Decider and the Verifier. Much of this is nicely commented :-)

#### VerificationInfo

The VerificationInfo for a Bouncer is quite complex. At the highest level, it consists of:
- a starting point: run the machine for a given number of steps to reach this;
- a sequence of RunDescriptors; execute each of these RunDescriptors to advance through the Cycle.

To describe the format of the Decider Verification File (dvf) in detail, we use the following basic data types:
- `int`: 32-bit signed integer
- `uint`: 32-bit unsigned integer
- `short`: 16-bit signed integer
- `ushort`: 16-bit unsigned integer
- `byte`: 8-bit signed integer
- `ubyte`: 8-bit unsigned integer

All the types are stored in big-endian format.

The Decider Verification File has the following structure:

```
dvf:
  uint nEntries
  VerificationEntry[nEntries]

VerificationEntry:
  uint SeedDatabaseIndex
  uint DeciderTag = NEW_BOUNCER (7) -- BOUNCER (6) is obsolete
  uint VerifInfoLength -- length of VerificationInfo
  VerificationInfo

VerificationInfo: -- information required to verify a perticular Bouncer
  byte BouncerType  -- Unilateral=1, Bilateral=2, Translated=3 (just for info -- not checked)
  ubyte nPartitions -- usually 1, but can be up to 4 (so far)
  ushort nRuns      -- usually 2, but can be up to 156 (so far)

  uint InitialSteps    -- Steps to reach the start of the Cycle
  int InitialLeftmost  -- Leftmost cell visited at start of Cycle
  int InitialRightmost -- Rightmost cell visited at start of Cycle

  uint FinalSteps      -- Steps to reach the end of the Cycle
  int FinalLeftmost    -- Leftmost cell visited at end of Cycle
  int FinalRightmost   -- Rightmost cell visited at end of Cycle

  ushort RepeaterCount[nPartitions] -- the Repeater count for each partition
                                    -- remains constant throughout the cycle
  TapeDescriptor InitialTape   -- Tape contents and state at start of Cycle
  RunDescriptor RunList[nRuns] -- Definition of each Run

RunDescriptor:
  ubyte Partition -- Partiton which the Repeaters traverse
  Transition RepeaterTransition
  Transition WallTransition

Transition: -- defines a transition from an initial tape segment to a final tape segment
  ushort nSteps
  Segment Initial -- Initial.TapeHead must be strictly contained in Tape
  Segment Final   -- Final.TapeHead may lie immediately to the left or right of Tape

Segment: -- a short stretch of tape, with state and tape head
  ubyte State
  short TapeHead -- relative to Tape[0]
  ByteArray Tape

TapeDescriptor:
  -- Wall[0] Repeater[0] ... Wall[nPartitions-1] Repeater[nPartitions-1]  Wall[nPartitions]
  -- For each partition, the number of repetitions of each Repeater remains unchanged
  -- throughout the Cycle, and is found in the RepeaterCount array in the VerificationInfo
  ubyte State
  ubyte TapeHeadWall
  short TapeHeadOffset -- Offset of tape head relative to Wall[TapeHeadWall]
  ByteArray Wall[nPartitions + 1]
  ByteArray Repeater[nPartitions]

ByteArray:
  ushort Len
  ubyte Data[Len]
```

#### The Verification Process

The process basically consists of maintaining a Turing Machine `TM` and a `TapeDescriptor TD` as you execute the runs in a cycle, according to the `RunDescriptor` entries. At the end of the cycle, you check that the resulting `TD` faithfully describes the final `TM`.

The program `VerifyBouncers.exe` is compiled from:
- `VerifyBouncers.cpp`, which handles command-line arguments, memory allocation, and file I/O;
- `BouncerVerifier.cpp`, which contains code used only by the Verifier;
- `Bouncer.cpp`, which contains code common to the Decider and the Verifier.

All functions referenced in the following description can be found in `Bouncer.cpp`.

##### 1. Initialisation
- Read `SeedDatabaseIndex, DeciderTag, VerifInfoLength`. Check that `DeciderTag` is `NEW_BOUNCER (7)` and initialse `TM` from `SeedDatabaseIndex`.<br>
- Read `BouncerType, nPartitions, nRuns` and check that they are within bounds (see description above). Set *N*=`nPartitions`.<br>
- Read `InitialSteps, InitialLeftmost, InitialRightmost`; execute `InitialSteps` steps in `TM` and check that `InitialLeftmost, InitialRightmost` are correct (this check is important to distinguish Bouncers from Bells).<br>
- Read the `RepeaterCount` array *C*<sub>0</sub>,...,*C*<sub>*N*-1</sub>.<br>
- Read `InitialTape` and check that it accurately describes `TM` (function `CheckTape`).

##### 2. RunDescriptor Verification
For each `RunDescriptor`, read `Partition, RepeaterTransition,` and `WallTransition`, and perform 2(a) to check `RepeaterTransition` and 2(b) to check `WallTransition`.

##### 2(a). RepeaterTransition
`RunDescriptor` can describe a left-to-right run or a right-to-left run. The verification process is conceptually the same for each, but we describe them separately for clarity. If `Partition` is equal to `TD.TapeHeadWall` the run is from left to right; if `Partition` is equal to `TD.TapeHeadWall - 1` the run is from right to left. Other values of `Partition` are not allowed.

We use `Stride` to denote the size of the repeating segment `TD.Repeater[Partition]`.

**Left-to-right**<br>
The tape head moves from the source wall *W*<sub>src</sub> =`TD.Wall[TD.TapeHeadWall]` to the destination wall *W*<sub>dest</sub> =`TD.Wall[TD.TapeHeadWall+1]` via the array of repeaters *Rep*<sup>*C*<sub>*P*</sub></sup>, where *p*=`Partition` and *Rep* =`TD.Repeater[Partition]`.<br>
Check that:
- `RepeaterTransition.Initial.State = RepeaterTransition.Final.State = TD.State`;
- `RepeaterTransition.Final.TapeHead = RepeaterTransition.Initial.TapeHad + Stride`;
- executing `RepeaterTransition.nSteps` steps transforms `RepeaterTransition.Initial` into `RepeaterTransition.Final` without leaving the tape segment except on the very last step.

Decompose `Tr.Initial.Tape` into three parts *A* || *R* || *B*, where |*A*|=`Tr.Initial.TapeHead` and |*Rep*|=`Stride`. (*A* and/or *B* can be empty.) To see what's going on, we can align *A* || *R* || *B* with the tape like this:
```
[W_src] [Rep] [W_dest]
  [ A ] [ R ] [ B ]
```
First, check that
- *A* matches the right-hand end of *W*<sub>src</sub>;
- *R* is equal to *Rep*;
- *B* matches the left-hand end of *W*<sub>dest</sub>;
- executing `RepeaterTransition.nSteps` steps transforms `RepeaterTransition.Initial` into `RepeaterTransition.Final` without leaving the tape segment except on the very last step.

Decompose `RepeaterTransition.Final` into *R'* || *A'* || *B*, where |*R'*|=|*R*| and |*A'*|=|*A*|. Note that *B* is unchanged (this should be checked).

So now we see that executing `RepeaterTransition` has the following consequences:
- *W*<sub>src</sub> loses |*A*| bytes from its right-hand end;
- *Rep* becomes *Rep'*;
- *W*<sub>dest</sub> has |*A'*| inserted at its left-hand end.

Make these changes to `TD.Wall[TD.TapeHeadOffset], TD.Repeater[Partition],` and `TD.Wall[TD.TapeHeadOffset + 1]`, and set:
- `TD.State = RepeaterTransition.Final.State`
- `TD.TapeHeadWall = Partition + 1`
- `TD.TapeHeadOffset = `|*A*|.

Now execute `RepeaterTransition.nSteps` steps in `TM`, and check that `TD` accurately describes the resulting `TM`.

**Right-to-left**<br>
The tape head moves from the source wall *W*<sub>src</sub> =`TD.Wall[TD.TapeHeadWall]` to the destination wall *W*<sub>dest</sub> =`TD.Wall[TD.TapeHeadWall-1]` via the array of repeaters *Rep*<sup>*C*<sub>*P*</sub></sup>, where *p*=`Partition` and *Rep* =`TD.Repeater[Partition]`.<br>
Check that:
- `RepeaterTransition.Initial.State = RepeaterTransition.Final.State = TD.State`;
- `RepeaterTransition.Initial.TapeHead = RepeaterTransition.Final.TapeHad + Stride`;
- executing `RepeaterTransition.nSteps` steps transforms `RepeaterTransition.Initial` into `RepeaterTransition.Final` without leaving the tape segment except on the very last step.

Decompose `Tr.Initial.Tape` into three parts *A* || *R* || *B*, where |*A*|=`Tr.Final.TapeHead` and |*Rep*|=`Stride`. (*A* and/or *B* can be empty.) To see what's going on, we can align *A* || *R* || *B* with the tape like this:
```
[W_dest] [Rep] [W_src]
   [ A ] [ R ] [ B ]
```
First, check that
- *A* matches the right-hand end of *W*<sub>dest</sub>;
- *R* is equal to *Rep*;
- *B* matches the left-hand end of *W*<sub>src</sub>;
- executing `RepeaterTransition.nSteps` steps transforms `RepeaterTransition.Initial` into `RepeaterTransition.Final` without leaving the tape segment except on the very last step.

Decompose `RepeaterTransition.Final` into *A* || *B'* || *Rep'*, where |*R'*|=|*R*| and |*B'*|=|*B*|. Note that *A* is unchanged (this should be checked).

So now we see that executing `RepeaterTransition` has the following consequences:
- *W*<sub>src</sub> loses |*B*| bytes from its left-hand end;
- *Rep* becomes *Rep'*;
- *W*<sub>dest</sub> has |*B'*| appended to its right-hand end.

Make these changes to `TD.Wall[TD.TapeHeadOffset], TD.Repeater[Partition],` and `TD.Wall[TD.TapeHeadOffset - 1]`, and set:
- `TD.State = RepeaterTransition.Final.State`
- `TD.TapeHeadWall = Partition`
- `TD.TapeHeadOffset = `the original size of *W*<sub>dest</sub>.

Now execute `RepeaterTransition.nSteps` steps in `TM`, and check that `TD` accurately describes the resulting `TM`.

##### 2(b). WallTransition
`WallTransition` describes the transition to apply to `TD.Wall[TD.TapeHeadWall]`.<br>
Check that:
- `WallTransition.Initial.State = TD.State`;
- `WallTransition.Initial.Tape` matches `TD.Wall[TD.TapeHeadWall]` byte for byte;
- `WallTransition.Initial.TapeHead` is equal to `TD.TapeHeadOffset`;
- executing `WallTransition.nSteps` steps transforms `WallTransition.Initial` into `WallTransition.Final` without leaving the tape segment except on the very last step;
- the last step leaves the tape segment to the left or the right.

Update `TD`:
- set `TD.State = WallTransition.Final.State;
- set `TD.Wall[TD.TapeHeadWall] = Tr.Final.Tape`;
- set `TD.TapeHeadOffset = Tr.Final.TapeHead`.

Now execute `WallTransition.nSteps` steps in `TM`, and check that `TD` accurately describes the resulting `TM`.

##### 3. Inter-Transition Verification (Inductive Proof only)
We say that `Transition Tr2` *follows on from* `Transition Tr1` if:
 - `Tr2.Initial.State = Tr1.Final.State`
 - After aligning the tapes so that `Tr1.Final.TapeHead` and `Tr2.Initial.TapeHead` are equal, the tapes agree with each other on the overlapping segment. So for instance if `Tr1.Final` is `001[1]01` and `Tr2.Initial` is `1[1]0110`, we align them:

```
   001[1]01
     1[1]0110
```

and check that the overlap is the same (`1[1]01`) in both tapes (function `CheckTransition`).

Now check that for each `RunDescriptor`, `RepeaterTransition` follows on from itself; and `WallTransition` follows on from `RepeaterTransition`. And check that for each `RunDescriptor` except the first, `RepeaterTransition` follows on from `WallTransition` from the previous `RunDescriptor`. And to complete the Cycle, check that the `RepeaterTransition` of the first `RunDescriptor` follows on from the `WallTransition` of the last `RunDescriptor` (function `CheckFollowOn`).

##### 6. Complete the Cycle
`TapeDescriptor TD1` in the last `RunDescriptor` describes the state of the machine after it has executed all the `Transitions` in the Cycle. Now adjust `InitialTape` by appending a single `Repeater` to each `Wall` (i.e. set `InitialTape.Wall[i] += InitialTape.Repeater[i]` for each partition `i`). After doing this, and adjusting the `Leftmost`, `Rightmost`, and `TapeHeadOffset` fields accordingly (functions `ExpandTapeLeftward` and `ExpandTapeRightward`), `InitialTape` should describe exactly the same tape as `TD1` (function `CheckTapesEquivalent`).

This is enough to conclude that the Cycle repeats indefinitely, and the machine is therefore a genuine Bouncer. (For this conclusion to be valid, it is important to note that the function `CheckTapesEquivalent` checks that two `TapeDescriptors` define the same tape contents for any values of the `RepeaterCount` array that are greater than or equal to the values in the current Cycle.)
