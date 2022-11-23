This is a Decider and Verifier for Bouncers. With time limit 100,000, it decides 1,405,856 (91%) of the 1,538,624 machines left undecided by the Backward Reasoning decider, taking about three and a half minutes.

The output file Bouncers.dvf is too large to commit to GitHub; run Run.bat (on Windows) to generate it.

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

To find Bouncers, we run a machine for a number of cycles (typically 100,000 or more), saving all Records as we encounter them (a Record is when the tape head reaches a cell that it has never visited before). If we find four Records with identical state whose tape heads are in arithmetic progression, but whose step counts are in quadratic progression, then we very probably have a Bouncer (and if it's not a Bouncer, it's almost certainly a Bell -- see below).

Now we run the machine for two more Cycles, saving the state and tape head of each step. Then we compare these two Cycles, looking for repeated Runs where the second Cycle is identical to the first except for an extra Repeater. If we can successfully match up each run in the first Cycle with a corresponding run in the second Cycle, we very probably have a Bouncer.

When I say "very probably", I mean that if it passes all the tests during the generation of the Verification Data, then it is a Bouncer; and if it passes enough of these tests, but not all, then it is almost certainly a Bell (e.g. [#73261028](https://bbchallenge.org/73261028)). A Bell is, loosely speaking, a sequence of Bouncers each of which gets interrupted in its journey to infinity and has to start again from scratch, growing exponentially the while. The BouncersDecider program has the option to output binary and text files containing the Probable Bells; no proof is provided, but such files will be useful for the development of any Bell Decider.

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
  uint DeciderTag = BOUNCER (6)
  uint VerifInfoLength -- length of VerificationInfo
  VerificationInfo

VerificationInfo: -- information required to verify a perticular Bouncer
  byte BouncerType  -- Unilateral=1, Bilateral=2, Translated=3
  ubyte nPartitions -- usually 1, but can be up to 4 (so far)
  ubyte nRuns       -- usually 2, but can be up to 156 (so far)

  uint InitialSteps    -- Steps to reach the start of the Cycle
  int InitialLeftmost  -- Leftmost cell visited at start of Cycle
  int InitialRightmost -- Rightmost cell visited at start of Cycle

  uint FinalSteps      -- Steps to reach the end of the Cycle
  int FinalLeftmost    -- Leftmost cell visited at end of Cycle
  int FinalRightmost   -- Rightmost cell visited at end of Cycle

  ushort RepeaterCount[nPartitions] -- the Repeater count for each partition
                                    -- remains constant rhtoughout the cycle
  TapeDescriptor InitialTape   -- Tape contents and state at start of Cycle
  RunDescriptor RunList[nRuns] -- Definition of each Run

RunDescriptor:
  ubyte Partition -- Partiton which the Repeaters traverse
  ushort RepeaterCount -- Number of times to execute each RepeaterTransition
  Transition RepeaterTransition
  TapeDescriptor TD0 -- Tape contents and state after executing the RepeaterTransitions
  Transition WallTransition
  TapeDescriptor TD1 -- Tape contents and state after executing the WallTransition

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

#### The Verification Process: the Easy Parts

##### 1. Initialisation
Run the machine for `InitialSteps` steps. Check that `InitialLeftmost` and `InitialRightmost` are correct (this is important to distinguish Bouncers from Bells). Check that `InitialTape` accurately describes the machine (state, tape head, and tape).

##### 2. Per-Transition Verification
For each `RepeaterTransition` and `WallTransition`, verify that executing `nSteps` from the `Initial` segment takes you to the `Final` segment, without leaving the segment boundaries.

##### 3. Inter-Transition Verification
We say that `Transition Tr2` *follows on from* `Transition Tr1` if:
 - `Tr2.Initial.State = Tr1.Final.State`
 - After aligning the tapes so that `Tr1.Final.TapeHead` and `Tr2.Initial.TapeHead` are equal, the tapes agree with each other on the overlapping segment. So for instance if `Tr1.Final` is `001[1]01` and `Tr2.Initial` is `1[1]0110`, we align them:

```
   001[1]01
     1[1]0110
```

and check that the overlap is the same (`1[1]01`) in both tapes.

Now check that for each `RunDescriptor`, `RepeaterTransition` follows on from itself; and `WallTransition` follows on from `RepeaterTransition`. And check that for each `RunDescriptor` except the first, `RepeaterTransition` follows on from `WallTransition` from the previous `RunDescriptor`. And to complete the Cycle, check that the `RepeaterTransition` of the first `RunDescriptor` follows on from the `WallTransition` of the last `RunDescriptor`.

##### 4. TapeDescriptor Verification
For each `RunDescriptor`, execute its `RepeaterTransition` `RepeaterCount[Partition]` times, and check that `TD0` correctly describes the state of the machine; execute its `WallTransition` once, and check that `TD1` correctly describes the state of the machine.

##### 5. Complete the Cycle
`TapeDescriptor TD1` in the last `RunDescriptor` describes the state of the machine after it has executed all the `Transitions` in the Cycle. Now adjust `InitialTape` by appending a single `Repeater` to each `Wall` (i.e. set `InitialTape.Wall[i] += InitialTape.Repeater[i]` for each partition `i`). After doing this, `InitialTape` should describe exactly the same tape as `TD1`. (You will have to adjust `Leftmost`, `Rightmost`, and `TapeHeadOffset` fields for this, so perhaps this step should come under "mostly Easy".)

#### The Verification Process: the Hard Parts

To complete the Verification Process, we have to check that each `Transition` really does transform its preceding `TapeDescriptor` to its following `TapeDescriptor`.

TO BE CONTINUED...
