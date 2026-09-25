# BMO Linger — the M2 listening set

**Rendered on ICE QUEEN**, 2026-09-24, from `frosty-linger-m2-er` at the
commit that pinned the MIX law and the flatness rule. The set lives in the
gitignored `packages/reverb-listening/set-2026-09-24/` on ICE QUEEN and
nowhere in the tree. **Nothing in it has been heard by the machine that made
it**; that is what the set is for.

Every Linger file is the engine that ships, rendered by `measure_reverb
render` at the schema defaults plus the overrides named in the file — MIX 50
unless the name says `wet`, REVERB off (the tail is M3's), ER at its default
−6 dB unless the name says an ER level. Sources are the clips already on
ICE QUEEN under `D:\VISUAL\PLUGINS\MIX RACK\`, copied into `sources/` at
their own 44.1 kHz; the renders keep that rate.

## Sources

| file | from | what it is |
|---|---|---|
| `vocal-fuji.wav` | SATURATOR / fuji NOT SATURATED | the house dry vocal, 20 s, starts silent |
| `vocal-failure.wav` | BMO TUNE / FAILURE / Antares Failure DRY | the second dry vocal, 19 s |
| `guitar.wav` | DEQ / REFs / DEQ ref GTR | acoustic guitar, 12 s |
| `drum-room.wav` | DEQ / REFs / DEQ ref ROOM | a drum room, 10.7 s |
| `drum-loop.wav` | DEQ / REFs / PHRYGIAN D DRUM LOOP | the dry loop, 7.3 s — the snare lives here |
| `synth.wav` | DEQ / REFs / PHRYGIAN D SYNTH | a held synth, 7.3 s |
| `808.wav` | DEQ / REFs / PHRYGIAN D 808 audio | the 808, 7.3 s — low end against the cluster |

The DEQ pass's A/B renders were EQ moves on these same dry files (Frosty,
2026-09-24) and are not needed here.

## What to listen for, and which files answer it

The items are 11 §6's ER-only list and the handoff's, in that order. Play
the `-mix50` files against the source; play the `-wet` files alone.

1. **Rap vocal at 15–25 ms, tail off: depth, not reverb.**
   `vocal-failure-room12-mix50` (first reflections at 1.8, 10, 11.8 ms) and
   `vocal-failure-room20-mix50` (the same cluster stretched: 3, 17, 20 ms).
   Also `vocal-fuji-room12-mix50`, `vocal-fuji-room20-mix50`, and the two
   `-wet` files for what the cluster alone is.
2. **Snare, dry and close: flam?** `drum-loop-room12-mix50` and
   `drum-loop-room12-wet`. If a snare hit reads as two, that is the flamming
   rule failing by ear where it passed by number.
3. **Drum room across SIZE: no combing when summed, no chorus.**
   `drum-room-size6 / 12 / 24 / 48-mix50`. Sum each to mono as well.
4. **Acoustic guitar across DENSITY and ER HI-CUT.**
   `guitar-density0 / 50 / 100-mix50`: discrete taps to a wash, level held.
   `guitar-hicut20000 / 7000 / 3000-mix50`: the anti-boxiness control.
5. **Ambience as a depth control, tail off, ER level the only distance.**
   `vocal-failure-ambience-er-3 / -9 / -15 / -21` and
   `drum-loop-ambience-er-9`. The source should move back without wash.
6. **Mono at VARIATION 0 and 6.** `vocal-failure-room12-var0 / var6-mix50`
   and `drum-loop-room12-var0 / var6-mix50`. Sum to mono: 0 should barely
   change, 6 should lose its width and keep its level (it is built mono-flat,
   not mono-empty — see the M2 note's open point 3).
7. **Blend, before its position is frozen.** `vocal-failure-room12-ermode0 /
   1 / 2-wet` and `guitar-room12-ermode0 / 1 / 2-wet`: Taps, Energy, Blend,
   ER alone. Blend is the one nobody has heard; if it does not earn its
   place, say so now, because the count of ER MODE is permanent at ship.
8. **A held note and a hall.** `synth-room12-mix50`, `synth-hall34-mix50`.
9. **Low end.** `808-room12-mix50`, `808-room24-mix50`, `808-room12-wet`,
   `808-ambience-er-9`, `808-room12-var6-mix50`: does the cluster cloud or
   comb the bottom, and does VARIATION 6 keep it in mono. `refs/808-lexicon224-wet`
   for the reference.

## The reference leg

`refs/` holds the same four clips (`vocal-failure`, `drum-loop`, `guitar`,
`drum-room`) through the two licensed stand-ins at their factory state:
`-lexicon224-wet` (UAD Lexicon 224, SmHall A, wet solo, its own 24 ms
predelay and 2.2 s tail), `-lexicon224-mix50` (the same with wet solo off,
which the plugin mixes at its own 50 %), and `-verbsuite-wet` (Slate VerbSuite Classics, **guitar and drum-room only**:
headless it rendered the vocal as silence and the drum loop 30 dB down, on
every try, so those two are not in the set). **These have tails and ours
does not**, so they are not
an A/B on the early reflections; they are what a dense, decorrelated onset
sounds like on this material, which is the Reference-B side of the thesis.
Renaissance Reverb and Valhalla are still to be installed.

## Levels

Nothing is normalised. The `-mix50` files carry the dry at unity, so they sit
at the source's level with the cluster on top; the `-wet` files are the
cluster alone at ER 0 dB, 8–13 dB under the source. Turn playback down for
the references' wet files, which are at the plugins' own wet levels.

## What the machine already measured of these settings

See `linger-m2-er-2026-09-24.md`: every setting above is inside the ranges
the tests cover, the DENSITY sweep is level-flat to 0.03 dB, γ at VARIATION
0 and 6 is 0.99 and 0.00, and the ER-only ripple at DENSITY 100 % is 4.6 dB
octave-smoothed. What the numbers cannot say is whether any of it sounds
like a room.
