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

The DEQ pass's A/B renders were EQ moves on these same dry files, and the 808
stays out because 808s do not usually get verb (Frosty,
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

## Friday 25 September: the LINGER folder

Frosty recorded into `D:\VISUAL\PLUGINS\MIX RACK\LINGER` on ICE QUEEN, at
44.1 or 48 kHz (both fine: every tool runs at the file's own rate). Copied into
`sources/` as `rap-vocal-01` (73 s), `rap-vocal-02` (62 s), `snare-01` (one
hit), `snare-02` (several hits, mono), `lead-vocal-03` (6.4 s, peaks at
0 dBFS), `guitar-02` (16 s), `held-note` (10 s), `sine-1k` (10 s, −6.4 dBFS),
`woodblock` (6 s). `measure_reverb stats` prints these figures.

Rendered through Linger, ER only, same conventions as above: both rap vocals
at Room 12 and 20 m, wet, and through Ambience at ER −9; both snares and the
woodblock at Room 12 mixed, wet, wet at DENSITY 100, and Hall 34; the third
lead vocal mixed and wet; the second guitar across DENSITY and HI-CUT; the held
note through Room and Hall; the sine wet through Room and Hall. **The
lead-vocal-03 mix file peaks at +1.1 dBFS**: turn it down. 67 Linger files.

Through the UAD units: rap-vocal-01, snare-02, lead-vocal-03, guitar-02,
held-note and sine-1k each through Lexicon 224 (wet and mixed), RealVerb-Pro
(wet) and the Precision Reflection Engine (wet and mixed); rap-vocal-01 and
snare-02 through Pure Plate. 54 reference files. RealVerb-Pro on the third
lead vocal peaks at +0.9 dBFS.

The held-note and sine files are for M3's modulation question and are here
early: nothing modulates yet, so Linger's versions of them are the cluster
alone.

## Friday 25 September: the Renaissance pass in Live

Bounced by Frosty in Ableton Live on ICE QUEEN, RVerb (Waves V17 shell,
installed 2026-09-25), 48 kHz, 32-bit float, all individual tracks,
into `D:\VISUAL\PLUGINS\MIX RACK\LINGER\RVERB\`.

- **Decay is 1.81 s, not 1.8.** RVerb's Decay control steps from 1.81 to
  the next value and will not take 1.8 typed. Files keep the board's `_1.8s`
  names; the figure is 1.81 everywhere.
- **Type map** (Frosty, 25th): room → Room, chamber → Chamber, hall →
  Hall 1, cavern → Church, plate → Plate 1, ambience → Room at Size
  **19.9 %**.
- **Extras:** Hall 2 and Church, each at RVerb's own default settings, so
  every relevant model is in the set.
- **Preset column:** the factory preset built on the same type, loaded
  as-is with its own decay (Decay not set to 1.81).
- **Presets used:** room Bedroom, chamber Concrete Venue Empty, hall Grand
  Hall, cavern (Church) Cathedral, plate Vocal Plate, ambience Phonebooth,
  Hall 2 1000 Seat Cavern.
- **Pass A** (the impulse through every type): 28 files in
  `packages/reverb-listening/refs/renaissance/`, named
  `refA_<row>_<full|er|tail>_1.81s` and `refA_<row>_full_preset-<name>`,
  with `hall2` as the extra row and Church filed under `cavern`. 5.5 s each;
  every file carries signal, peaks −17 to −40 dBFS. The first export was
  silent on every track but one because a track was soloed; it was
  re-bounced, and only the second export is used.
- **RVerb at its default settings** (Frosty, 25th), matching the UAD units'
  factory-state renders: `refA_default_full.wav`.
- **`refA_default_full`'s ER is at −2 dB**, which is RVerb's default. Every
  other Renaissance file has ER and Reverb at 0 dB.
- **Pass B** (the dry clips through RVerb): 14 files in `set-2026-09-24/refs/`,
  on Hall 1 at 1.81 s with ER and Reverb at 0 dB. `sine-1k` and `held-note`
  are wet through Hall 1 and Plate 1 (`-rverb-hall-wet`, `-rverb-plate-wet`);
  `vocal-failure`, `rap-vocal-01`, `snare-02`, `drum-room` and `guitar` are
  each at 100 % (`-rverb-wet`, for a send) and 50 % (`-rverb-mix50`, on the
  channel), which were Frosty's two use cases. Every file is 77 s and padded
  with silence after its clip; the 44.1 kHz sources were resampled to 48 kHz
  by Live.
- **RVerb's 50 % is equal-power: dry and wet both at −3 dB.** This was
  measured on ICE QUEEN on `rap-vocal-01` by fitting the mix50 file as a
  mix of the dry source and the wet file. The fit leaves a residual of
  −124 dB, so the result is exact, and the renders are sample-aligned with
  their sources. Linger's law keeps both legs at unity at 50 %, so **Linger's
  mix50 files are 3 dB hotter than RVerb's** in both the dry and the wet.
  Turn Linger down 3 dB when comparing the two by ear.
