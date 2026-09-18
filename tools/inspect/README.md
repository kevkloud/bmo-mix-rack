# Render inspection harness

Answers the questions a look at a panel raises but cannot settle: what colour
is that exactly, is that one shape or three, did this refactor move a pixel.

Written in C# and **deliberately not wired into `tools/CMakeLists.txt`**, for
the same reasons as `tools/measure/renders` — see its README. A tool for
looking at renders should never be able to fail a plugin build.

These existed as throwaway PowerShell in a session scratchpad and died with
it, which cost the next session half an hour to rebuild. This is them written
down.

## Running it

    csc -out:Inspect.exe -r:System.Drawing.dll Inspect.cs   # Framework64/v4.0.30319/csc.exe
    ./Inspect.exe <mode> ...

| mode | what it does |
|---|---|
| `scan`  | run-length along one row or column: start, end, width, exact hex |
| `hist`  | exact-colour histogram over a box, with L\* per colour |
| `crop`  | crop and magnify, nearest-neighbour |
| `sheet` | several renders side by side with labels |
| `hash`  | SHA-256 of the pixels, for before/after on a refactor |
| `ratio` | contrast and L\* between colours, every pair both ways |
| `gaps`  | runs of bare plate down a panel — the empty bands a reader sees |

`ratio` takes either a hex or a pixel out of a render:

    ./Inspect.exe ratio "#8d8d98" snapshots/rack.png:260,364

`gaps` takes the render and, optionally, the smallest band worth printing, the
header height and the render scale:

    ./Inspect.exe gaps snapshots/util-dark.png            # min 10, header 52, scale 2

The last two are properties of `tools/snapshot`, not of the PNG — it renders at
2x and a product editor puts `ProductHeader::kHeight` 28 plus a 24 px preset
strip above the panel — so they are **printed on every run** rather than assumed
quietly. Pass them if either ever moves.

## Why each one exists

Each mode is the answer to a specific way of being wrong, all of which
happened. `testing-notes/ui-editor-handoff.md` §6 is the long version.

- **`scan` is the one that matters most.** Squinting at a magnified crop
  cannot tell you two inks are the same to within 1.01:1. The dark band
  selector looked like a ring; scanned, rows 355-376 of `ring-dark.png` come
  back `#8d8d98`, `#8e8e93`, `#8d8d98` running together as a 22 px slab,
  because the hairlines and the face were the same colour. No amount of
  looking produces that number.
- **`hist` replaces "the darkest pixel in that box".** Two inks 128 apart in
  hex can be a third of an L\* apart: `#497450` and `#317290` are 44.9 and
  45.2, which is 1.01:1. Darkest-pixel is a coin flip between a caption and
  the rule beside it. Count exact matches instead.
- **`crop` never smooths.** Interpolation would invent colours that are not in
  the render, which is the one thing an inspection tool must not do.
- **`sheet` is the cheap answer to "is this the same as that"**, and to
  showing candidate layouts rather than describing them. Its ground is
  deliberately neither plate, so it cannot be mistaken for part of a render.
- **`hash` settles a refactor instead of arguing it.** Render every panel,
  refactor, render again, compare. Byte-identical or it was not a refactor.
  It hashes pixels rather than file bytes, because a PNG encoder is free to
  vary everything around them.
- **`gaps` is the answer to "is this panel too airy", which the layout dump
  cannot give you.** `ui_layout_tests --dump` prints control *boxes*, and a
  panel's boxes are very nearly contiguous — on BMO Util the rule under VOLUME
  and the PAN box are one pixel apart — while the ink inside them is not,
  because a knob box carries padding above its face and below its caption. Read
  from the dump alone, Util has no empty band worth the name. Rendered, its
  worst is 46 px against BMO EQ's 21. Boxes are what a layout test asserts, ink
  is what a reader sees, and only the render knows the difference.

  Written during the 2026-09-14 UI pass on **AURORA**, where the first pass at
  the question was made from the dump and got the wrong answer: the checklist
  had named the gap between the VOLUME rule and PAN, which is the 1 px one.

  It ranks the suite in one line each, and the numbers are the same in both
  appearances, as layout should be:

  | panel | largest empty band | where |
  |---|---|---|
  | DEQ compact | 20 px | 340..359 |
  | BMO EQ | 21 px | 544..565 |
  | DEQ expanded | 30 px | 536..565 |
  | BMO Util | 46 px | 520..565 |
  | BMO Opto | 48 px | 82..130 |
  | BMO Saturator | 66 px | 91..156 |
  | BMO Dimension | 72 px | 180..252 |
  | BMO Tune RT | 110 px | 273..382 |
  | BMO Vcomp | 190 px | 480..669 |

  Vcomp's 190 is its standard mode, where the foot below COMPLEX / ARC holds
  controls that are not shown; render it with `complex=1` to see the other
  half. That is the general warning about this mode: **it measures the state
  you rendered, not the panel.**

  A hairline rule breaks a band, because it is a row with ink in it. Util's
  empty foot prints as 46 and 31 either side of the rule centred on 566, not as
  one run of 78 — read the two together when a rule sits between them.

- **`ratio` prints L\* beside the contrast** because below about L\* 20 the
  ratio stops discriminating: from the dark plate the most contrast available
  by going darker, all the way to black, is 1.29:1. The dark set's structural
  greys are spaced by lightness for that reason.

## Things it cost a wrong answer to learn

- **`crop` and `sheet` refuse to overwrite.** A stale read of a path that had
  only been re-rendered in the reader's head cost a full debugging cycle
  chasing a bug that did not exist. Render to a unique filename; the tool now
  enforces it rather than trusting you to remember.
- **A ratio without a named ground is not a measurement.** "The marker is
  4.67:1" was true and useless. `ratio` prints every pair both ways round so
  the ground is always on the line with the number.
- **Both appearances, every time.** Half the faults on the `ui-editor` branch
  existed in one only. A change measured on the pale plate has not been
  checked.
- **Check what the baseline actually is.** Two "regressions" were a baseline
  with different switch states, and a baseline that predated an intervening
  commit. `hash` is faster than re-deriving that by eye.
- An unknown mode is refused rather than quietly doing nothing, the way
  `tools/snapshot`'s `realValueFor` now refuses a mistyped choice name. A
  no-op that prints nothing reads as an answer.
