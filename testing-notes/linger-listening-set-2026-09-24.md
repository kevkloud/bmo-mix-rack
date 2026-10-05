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
ICE QUEEN in the owner's reference folder (outside the repository), copied into `sources/` at
their own 44.1 kHz; the renders keep that rate.

## Sources

| file | from | what it is |
|---|---|---|
| `vocal-songb.wav` | SATURATOR / songb NOT SATURATED | the house dry vocal, 20 s, starts silent |
| `vocal-songa.wav` | BMO TUNE / SONG A / Tuner A Song A DRY | the second dry vocal, 19 s |
| `guitar.wav` | DEQ / REFs / DEQ ref GTR | acoustic guitar, 12 s |
| `drum-room.wav` | DEQ / REFs / DEQ ref ROOM | a drum room, 10.7 s |
| `drum-loop.wav` | DEQ / REFs / Source set E DRUM LOOP | the dry loop, 7.3 s — the snare lives here |
| `synth.wav` | DEQ / REFs / Source set E SYNTH | a held synth, 7.3 s |

The song files and folders in this table are named by code; on disk they keep
their own names, and the key outside the repository maps the two.

The DEQ pass's A/B renders were EQ moves on these same dry files, and the 808
stays out because 808s do not usually get verb (Frosty,
2026-09-24) and are not needed here.

## What to listen for, and which files answer it

The items are 11 §6's ER-only list and the handoff's, in that order. Play
the `-mix50` files against the source; play the `-wet` files alone.

1. **Rap vocal at 15–25 ms, tail off: depth, not reverb.**
   `vocal-songa-room12-mix50` (first reflections at 1.8, 10, 11.8 ms) and
   `vocal-songa-room20-mix50` (the same cluster stretched: 3, 17, 20 ms).
   Also `vocal-songb-room12-mix50`, `vocal-songb-room20-mix50`, and the two
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
   `vocal-songa-ambience-er-3 / -9 / -15 / -21` and
   `drum-loop-ambience-er-9`. The source should move back without wash.
6. **Mono at VARIATION 0 and 6.** `vocal-songa-room12-var0 / var6-mix50`
   and `drum-loop-room12-var0 / var6-mix50`. Sum to mono: 0 should barely
   change, 6 should lose its width and keep its level (it is built mono-flat,
   not mono-empty — see the M2 note's open point 3).
7. **Blend, before its position is frozen.** `vocal-songa-room12-ermode0 /
   1 / 2-wet` and `guitar-room12-ermode0 / 1 / 2-wet`: Taps, Energy, Blend,
   ER alone. Blend is the one nobody has heard; if it does not earn its
   place, say so now, because the count of ER MODE is permanent at ship.
8. **A held note and a hall.** `synth-room12-mix50`, `synth-hall34-mix50`.

## The reference leg

`refs/` holds the same four clips (`vocal-songa`, `drum-loop`, `guitar`,
`drum-room`) through the two licensed stand-ins at their factory state:
`-plugD-wet` (Reverb plug D, its small-hall preset, wet solo, its own 24 ms
predelay and 2.2 s tail), `-plugD-mix50` (the same with wet solo off,
which the plugin mixes at its own 50 %), and `-plugG-wet` (Reverb plug G, **guitar and drum-room only**:
headless it rendered the vocal as silence and the drum loop 30 dB down, on
every try, so those two are not in the set). **These have tails and ours
does not**, so they are not
an A/B on the early reflections; they are what a dense, decorrelated onset
sounds like on this material, which is the Reference-B side of the thesis.
Reference A and Vendor 3 are still to be installed.

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

Frosty recorded into a LINGER folder in the reference folder on ICE QUEEN, at
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

Through the Vendor 4 units: rap-vocal-01, snare-02, lead-vocal-03, guitar-02,
held-note and sine-1k each through Reverb plug D (wet and mixed), Reverb plug F
(wet) and Reverb plug O (wet and mixed); rap-vocal-01 and
snare-02 through Reverb plug E. 54 reference files. Reverb plug F on the third
lead vocal peaks at +0.9 dBFS.

The held-note and sine files are for M3's modulation question and are here
early: nothing modulates yet, so Linger's versions of them are the cluster
alone.

## Friday 25 September: the Reference A pass in Live

Bounced by Frosty in Ableton Live on ICE QUEEN, Reference A (Shell B v17,
installed 2026-09-25), 48 kHz, 32-bit float, all individual tracks,
into a subfolder of that LINGER folder.

- **Decay is 1.81 s, not 1.8.** Reference A's Decay control steps from 1.81 to
  the next value and will not take 1.8 typed. Files keep the board's `_1.8s`
  names; the figure is 1.81 everywhere.
- **Type map** (Frosty, 25th; Reference A's types by their number in
  `docs/reverb/01`): room → type 3, chamber → type 4, hall → type 1, cavern →
  type 5, plate → type 6, ambience → type 3 at Size **19.9 %**.
- **Extras:** type 2 and type 5, each at Reference A's own default settings, so
  every relevant model is in the set.
- **Preset column:** the factory preset built on the same type, loaded
  as-is with its own decay (Decay not set to 1.81).
- **Presets used** (factory presets by handle; the key outside the
  repository has their names): room P1, chamber P2, hall P3, cavern (type 5)
  P5, plate P6, ambience P7, type 2 P4.
- **Pass A** (the impulse through every type): 28 files in
  `packages/reverb-listening/refs/refA/`, named
  `refA_<row>_<full|er|tail>_1.81s` and `refA_<row>_full_preset-<name>`,
  with `hall2` as the extra row and type 5 filed under `cavern`. 5.5 s each;
  every file carries signal, peaks −17 to −40 dBFS. The first export was
  silent on every track but one because a track was soloed; it was
  re-bounced, and only the second export is used.
- **Reference A at its default settings** (Frosty, 25th), matching the Vendor 4 units'
  factory-state renders: `refA_default_full.wav`.
- **`refA_default_full`'s ER is at −2 dB**, which is Reference A's default. Every
  other Reference A file has ER and Reverb at 0 dB.
- **Pass B** (the dry clips through Reference A): 14 files in `set-2026-09-24/refs/`,
  on type 1 at 1.81 s with ER and Reverb at 0 dB. `sine-1k` and `held-note`
  are wet through type 1 and type 6 (`-refA-hall-wet`, `-refA-plate-wet`);
  `vocal-songa`, `rap-vocal-01`, `snare-02`, `drum-room` and `guitar` are
  each at 100 % (`-refA-wet`, for a send) and 50 % (`-refA-mix50`, on the
  channel), which were Frosty's two use cases. Every file is 77 s and padded
  with silence after its clip; the 44.1 kHz sources were resampled to 48 kHz
  by Live.
- **Reference A's 50 % is equal-power: dry and wet both at −3 dB.** This was
  measured on ICE QUEEN on `rap-vocal-01` by fitting the mix50 file as a
  mix of the dry source and the wet file. The fit leaves a residual of
  −124 dB, so the result is exact, and the renders are sample-aligned with
  their sources. Linger's law keeps both legs at unity at 50 %, so **Linger's
  mix50 files are 3 dB hotter than Reference A's** in both the dry and the wet.
  Turn Linger down 3 dB when comparing the two by ear.

## Saturday 26 September: Frosty's verdicts

Heard by Frosty on ICE QUEEN through Monitors A, with mono
checks done on the interface and headphone amp (L+R). His answers are kept
word for word in the gitignored
`packages/reverb-listening/set-2026-09-24/answers.md`; this is the summary.

| # | Item | Verdict |
|---|---|---|
| 1 | Blend | Taps "sounds great, small room vibe"; Energy "sounds great, short verb vibe"; Blend "sound[s] like a slightly worse" Taps. **Cut** (his call, same day). |
| 2 | Room as depth | "depth". At 50 % mix, 12 m and 20 m are hard to tell apart; the wet files "definitely read as a tight bedroom/studio". Needs more than 50 % to judge. |
| 3 | Default width | "var 06 is much wider, 2 isnt enough to feel. 3 or 4 should be default." **VARIATION defaults to 4** (his call). |
| 4 | Snare flam | **No flam**: "one legible hit per actual hit". |
| 5 | Drum room across SIZE | At 50 %, not very different from each other. **No combing or chorus in mono**: "they sum well". |
| 6 | Guitar, DENSITY and ER HI-CUT | DENSITY "not sure". No boxiness arose in any render, so there was none for HI-CUT to fix. Needs a wetter bounce. |
| 7 | Ambience as distance | **Fails.** The source does not move back and there is no wash: "can't hear it working". |
| 8 | Mono at VARIATION 0 and 6 | "they sound the same" summed. Passes: 6 is built mono-flat. |
| 9 | Held note and hall | No answer. |

Decisions:

- **Tail ceiling: raise it**, so it goes to 40 s in M3.
- **IN HI-CUT: a parameter, with a clearer name**, to be named when the
  panel is designed.

What follows from these:

- **Ambience is `11` §6's M2 exit condition, and it did not pass.** The
  renders were the right test: MIX 50 keeps the dry at unity, and ER ran
  from −3 to −21 dB over it. So this is not a mix-level artefact. The
  wetter follow-up set carries it again, at VARIATION 4 and with a Room
  comparison, before anything is concluded.
- **50 % is too subtle for ER-only material.** Items 2, 5 and 6 all say so,
  so a follow-up set at 75 % and 100 % is rendered for them.

**Why Ambience could not move a source back, measured the same day on ICE
QUEEN.** `vocal-songa` (−21.8 dBFS RMS) was rendered wet-only with the
tail off. At **ER 0 dB, the fader's top**, the reflection cluster measures
−28.5 dBFS RMS in Ambience at its own defaults (8 m, DENSITY 40, SPREAD
30), and −30.7 dBFS RMS in Room 12 m. So at MIX 50 the cluster sits 6.7 dB
under the dry at best, and 8.9 dB under in Room. The first set's sweep,
ER −3 to −21, put it 10 to 28 dB under the dry. A reflection field that
quiet adds colour, not distance. Two things are possible from here, and
both are Frosty's call once he has heard the follow-up: give the ER fader
headroom above 0 dB, or raise the cluster's level for each type.

**The follow-up set** is `packages/reverb-listening/set-2026-09-26-wet/`:
44 files at the new defaults, VARIATION 4 and Taps, with the tail off.

- Room 12 and 20 m on `vocal-songa` and `rap-vocal-01`, at MIX 75 and 100.
- The drum room across SIZE at MIX 75 and 100.
- The guitar across DENSITY and ER HI-CUT at MIX 75 and 100.
- Ambience at its own defaults across ER 0 / −6 / −12 / −18 at MIX 50, on
  both vocals, with Room alongside.
- Ambience at ER 0 and −6 at MIX 75. There the dry is 6 dB down, so the
  cluster sits about level with it.

It has its own `answers.md`.

## Saturday 26 September, later: the wetter set, heard

Heard by Frosty on ICE QUEEN. **Item 1 was heard on the monitors, in stereo.
Items 2–5 were heard first on headphones with the headphone amp left in
mono by mistake**; Frosty caught it and heard them again in stereo. The
mono answers are kept, labelled, in `set-2026-09-26-wet/answers.md`, and
only the stereo ones below count. Round 1 was not affected: its width
verdict was made on the monitors, before the deliberate mono checks.

| # | Item | Stereo verdict |
|---|---|---|
| 1 | Room as depth, wetter | "100% definitely works"; with ER only it is "tough to tell at 75". |
| 2 | Drum room across SIZE | Reads at MIX 100; "hard to say at mix 75". |
| 3 | Guitar, DENSITY | "reads at 75 and 100% after listening in stereo". In mono it had been indistinguishable, because the mono sum cancels most of what DENSITY changes at VARIATION 4. **DENSITY's range stays as it is.** |
| 3 | Guitar, ER HI-CUT | Heard once, in mono: no boxiness, and 3000 against 7000 "too subtle for such a big difference". Measured in stereo it is 3–4 dB at 4–8 kHz. **12 dB/octave** is Frosty's call. |
| 4 | Ambience as distance | At MIX 50 the voice moves back, "subtley"; at MIX 75 Ambience at ER 0 and Room at ER 0 are clearly different. **This passes `11` §6's M2 exit condition.** How far it should go is left for M4 (Frosty's call). |
| 5 | VARIATION 4 | "good default width". |

**ER HI-CUT went to 12 dB/octave the same day**: two identical poles, each
solved to be −1.5 dB at the corner, so the pair is −3 dB where the knob
says. At four times the corner the pair is 4.2–5.0 dB under the old single
pole. On the impulse, going from 7 kHz to 3 kHz now takes 6.1 dB off at
8 kHz, where it took 4.2 dB before. At 4 kHz the change is small, because
that is under an octave above a 3 kHz corner. Six guitar files with the new
slope are in `packages/reverb-listening/set-2026-09-26-hicut12/`, not yet
heard.

**Withdrawn the same day: "250 Hz moved at the 7 kHz default" was the
analyser, not the sound.** `measure_reverb analyse` starts its 80 ms window
at the loudest reflection. Cutting more top can make a later reflection the
loudest, and the window then jumps from 113.1 ms to 120.0 ms and skips the
first 7 ms of the cluster. Every render whose window started at 120.0 ms
read about −5 dB at 250 Hz (old filter at 3 kHz −4.7, new at 7 kHz −5.0 and
at 3 kHz −4.7), and every one starting at 113.1 ms read about −2. Taking
the hi-cut out of the diffuser's normaliser, as a trial, changed nothing,
and it was reverted. The shift was never in the audio: a linear filter at
7 kHz cannot move 250 Hz relative to 1 kHz by 3 dB. Figures from this mode
that compare renders are only comparable when their windows start at the
same reference.

**12 dB/octave, heard:** Frosty listened to the six guitar files in
`set-2026-09-26-hicut12/` on ICE QUEEN: "12dB is much better."
