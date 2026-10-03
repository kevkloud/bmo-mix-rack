# BMO Opto

An optical-style levelling compressor. Two knobs do the work, and the release
looks after itself.

## The face

| Control | What it does |
|---|---|
| **COMP** | How hard the programme drives the cell. It lowers the threshold and nothing else; the ratio and knee belong to the mode. At 0 a correctly staged track passes untouched. The parameter is `crush`. |
| **MAKEUP** | Level after the cell, set by ear. There is no automatic makeup. The parameter is `level`, -24 to +24 dB. |
| **TELE / ELD** | Which circuit this instance is. One button carrying its own state. |
| **LINK** | One gain for both channels, decided by the louder of them. Off, each channel compresses on its own. |
| **COLOR** | A harmonic stage. In ELD it is a switch. In TELE it is always on, and the switch is shown disabled. |
| **IN / OUT / GR** | What the needle reads. |

## The two modes

| | TELE | ELD |
|---|---|---|
| ratio | about 3:1 | 10:1 |
| knee | soft, 16 dB | harder, 6 dB |
| topology | feedback: it listens to its own output | feedforward: it listens to the input |
| attack | about 10 ms, shortened by the loop | about 10 ms, quickening to 0.5 ms when it is far short of what a loud onset asks |
| release | 60 ms for a short hit, sliding towards 4 s the longer and harder it has been driven | 60 ms sliding towards 3 s |
| color | low-order, even-harmonic warmth, always on | a grittier odd-harmonic clip, switchable |

Both give the gain back between phrases and hold on when leaned on. That is
the release doing what the mode is for, and it is why there is no release knob.

## Things worth knowing

- **Changing mode, Link or Color does not click.** Each crosses over in 10 ms.
  The mode you are not listening to keeps following the programme, so
  switching back finds it where it would have been.
- **The meter's reduction scale stops at 24 dB.** At the deepest settings the
  module can apply more than that; the needle pins and the module carries on.
- **A loud spike does not leave the level turned down behind it.** What a
  short spike adds comes back within about a third of a second; what a held
  passage adds comes back slowly, as it should.
- **No latency.** Nothing is delayed, so the very front of a hard onset always
  gets part of the way through before the cell catches it.
- **A track should arrive at about -18 dBFS RMS**, peaks around -12. COMP at 0
  is transparent there. Fed a mix-bus level it will start to work on its own.

## Presets

Init, Gentle, Vocal Glue and Crushed <3. Each is level-matched to Init on a
test signal, so stepping through them changes the compression and not the
loudness.

## For contributors

`AGENTS.md` in this folder has the design record: what the module is built
on, what was tried and refused, and the faults its tests once missed.
