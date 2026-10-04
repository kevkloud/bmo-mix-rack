# Session compatibility

`SessionCompatTests.cpp` (ctest `session_compat`) restores sessions that a
**shipped release** saved, and checks this build reads them back the way that
release did. The schema tests pin ids, order, ranges and defaults; this is the
test that actually opens an old session.

## Where the fixtures came from

`SessionCompatFixtures_0_2_5.h` is generated, text only. It was made on
2026-10-03, on ICE QUEEN, from the `v0.2.5` tag: a worktree of the tag, built
in Release with only its test executables (`rack_tests`, `tune_panel_tests`),
and a small dumper program compiled with that tree's headers and defines and
linked against those targets' own objects. So the blobs are what 0.2.5's code
writes, not what today's code thinks 0.2.5 wrote. The dumper is not in the
repository; its steps are below.

For every product the release built -- BMO CEQ, Saturator, Util, Opto,
Dimension, DEQ, LTV Comp, BMO Tune RT and the rack -- it:

1. created the product's processor the way the product does
   (`products/<x>/Product.h`);
2. set every parameter to a legal value away from its default: normalised
   `0.2 + 0.6 * frac(0.61803 * (i + 1) + 0.1 * slot)` for parameter `i`, put
   through the parameter so it lands on a step; a bool flipped, a choice or a
   stepped value moved one position if it landed on its default;
3. for the rack, built two chains that between them hold every module the
   release's rack could host -- `ltvcomp, deq, util, eq` with the DEQ slot
   expanded, and all eight slots `opto, dim, sat, eq, util, sat, deq, ltvcomp`
   with the second DEQ expanded -- every slot set as above, parameters past the
   32nd lane included; the standalone DEQ was saved compact, the other way
   from how it opens;
4. called `getStateInformation` and kept the blob (base64) and every value as
   the release read it back, with its displayed text;
5. did the same again at defaults (the rack's two chains at defaults, and an
   empty rack);
6. restored every blob into a fresh processor of the same release and checked
   it came back, so a difference here is never the release failing to
   round-trip itself.

## What the test asserts

Each fixture is restored on the message thread, from a host thread while the
message thread runs, and again from this build's own re-save of the first.
Each time:

- every parameter the release had reads back the release's value, found by
  id -- exactly for a bool, a choice or a stepped float; within one part in a
  million (absolute below 1) for a continuous one;
- every parameter the release had is still at the release's position in
  `specs()`. The values restore by id even after a reorder, so without this a
  moved parameter would pass while every automation lane recorded against it
  pointed somewhere else;
- every parameter the release did not have is at this build's default;
- the rack holds the same modules in the same slots, each host lane the
  release had carries the release's value on it (a lane past the release's
  parameters reads what its module does), parameters past the 32nd lane
  restore, empty slots stay empty, and the expanded view restores;
- the defaults fixtures restore the release's defaults, so a default that has
  moved since is still honoured for a session that saved the old one. The
  test prints a `NOTE` for each moved default.

A mismatch prints the release, fixture, pass, slot, parameter id, the
expected value with its old text, and what this build reads. A difference
that is intended goes in `kKnownDifferences` in the test with its figures; it
prints on every run, and an entry that stops occurring fails.

## Adding a release

When a release ships (tag `vX.Y.Z`):

1. Make a detached worktree of the tag, initialise `libs/JUCE`, and configure
   it with `-DCMAKE_BUILD_TYPE=Release` (without it, a Windows build installs
   plugins into the system folder). Build only the test executables that
   compile every product the tag ships -- never a plugin target.
2. Build the dumper against that tree: its include paths and defines, its
   test targets' objects minus each target's own test source, and its asset
   library. Extend the product list and the rack chains to every product and
   module the tag has; keep the value rule above.
3. Run it, check its self-check is clean, and generate
   `SessionCompatFixtures_X_Y_Z.h` the same shape, in namespace
   `session_compat::vX_Y_Z`.
4. Include it in `SessionCompatTests.cpp` and add a `runRelease` line for it.
   Keep every older release's fixtures: a session from any shipped release
   must still open.
5. Show it can fail before committing: change one expected value, and swap
   two parameters in a module's `specs()`; both must fail, then revert.

No audio, binary or image file belongs in a fixture: states are base64 text.
