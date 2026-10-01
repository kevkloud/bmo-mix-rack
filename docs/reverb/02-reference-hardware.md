# Reference hardware for the reverb module

Scope: which classic hardware reverb *characters* fit a module whose core is a smooth,
dense, mix-friendly late-1990s algorithmic reverb with a separate early-reflection (ER)
section (call that core **REF-A**), with a modern vintage-flavoured plugin reverb
(**REF-B**) as secondary reference. Focus throughout is **how each device makes early
reflections**. Devices carry neutral labels; the key at the end maps them to products.

Every claim is tagged **[DOC]** (manual, patent, spec sheet, designer publication),
**[MEAS]** (published measurement) or **[ANEC]** (forum/press/recollection).

## 0. What the core already does

REF-A splits ER from tail: an ER section with its own level, size-linked spacing and
type, feeding/parallel to a diffused tail with pre-delay, decay, diffusion, density and
damping. Its ER are a *pattern selection* (seven spacing variations, usually linked to
size), not a freely edited tap list **[DOC]**. LF damping knee is documented as
16 Hz–1600 Hz **[DOC]**. So: hardware characters worth importing are ones that either
(a) give a *distinct ER personality* the core lacks, or (b) give a distinct *tail*
personality that the existing ER section can front.

The single most useful design idea in the literature comes from the designer of HW-1/2/3:
strong very early energy, extending roughly to 50 ms after the source stops, is what
creates distance; isolated lateral reflections around 25–40 ms can *reduce* spatial
impression **[DOC]**. The modern plugin-design counterpoint (REF-B's author) is that a
few dozen discrete ray-traced taps ignore real surface diffusion, so the early section is
better built as diffuse "Early Energy" than as taps **[DOC]**. Our module should support
both stances.

## 1. Device notes

**HW-1 — late-1970s US studio digital reverb (200-series).** Loved for lush, slightly
grainy halls and its plate/room programs; the first programmable studio reverb people
mixed *into*. ER are not a separately editable stage: the programs begin with a
pre-delay plus a short diffused cluster ahead of the recirculating tail **[ANEC]**.
Converter/bandwidth limits (companded ~12-bit, restricted top end) are a large part of the
character **[ANEC]**.

**HW-2 — mid-1980s US flagship studio digital reverb.** The reference "big room". Two
things matter for us. (i) Its **Shape** and **Spread** pair governs the *envelope* of the
early energy: Shape sets the contour of the initial rise/plateau, Spread sets how long
that early energy is smeared before the tail takes over **[DOC, manual]**. (ii) Its
**random-delay hall** adds randomised delay modulation, which suppresses metallic
artefacts and shortens *perceived* decay, most audibly at small Spread **[DOC]**. Its
ambience program is explicitly ER-first: with reverb level at zero you hear only the early
cluster, which stops abruptly **[DOC]**. Pre-echo/early-tap controls exist alongside
Shape/Spread **[DOC]**.

**HW-3 — 1980s–90s US rack family.** Same lineage, cheaper. Notable for exposing a small
bank of user-set **reflect delays/levels** (discrete taps) *in addition* to the diffused
early energy, and for a "split" structure where ER and tail can be routed and balanced
independently **[DOC]**.

**HW-4 — 1976 German quad studio digital reverb.** First commercial digital reverb.
Structure per its patent/reconstructions: pre-delay → three all-pass diffusers → a tank
of delay chains with damping in the feedback path → a decorrelator producing four outputs
**[DOC patent; ANEC for tank times ~80–120 ms]**. Crucially there are **no discrete ER**;
it behaves like a very fast, plate-like onset. Its successor changed the algorithm **[ANEC]**.

**HW-5 — 1957 German electro-mechanical plate.** No ER at all: near-instant echo density,
optional external pre-delay. Decay continuously variable from under 1 s to about 6 s via
the damper **[DOC]**; HF decay collapses to under ~1 s at 10 kHz regardless of damper
setting **[MEAS]**. The canonical vocal/snare sheen.

**HW-6 — early-1980s British digital reverb.** Famous for the gated/non-linear program and
for an "ambience" program sitting between a clean plate and a small hall, used to *extend*
another reverb **[DOC, manual]**. Its manual frames ER as spanning ~5 ms (small room) to
~200 ms (large hall) **[DOC]**. Non-linear programs are envelope-shaped dense clusters,
not tap patterns. Low sample rate/bandwidth is a signature **[ANEC]**.

**HW-7 — early-1980s German room simulator.** Modelled the *resonant/modal* behaviour of
enclosed air rather than ray paths, so density and modal distribution follow the stated
room volume; the "first reflection" is treated as the cue that sets perceived size
**[DOC, maker material]**. Extremely neutral, no obvious tap pattern. Later units
document 16-bit conversion with oversampling and wide internal word length **[DOC]**.

**HW-8 — early-1980s British broadcast/live reverb.** The most *explicitly ER-controllable*
classic: a single **Pattern** knob, 0–9, sets early-reflection density, described in the
manual as moving from low-density and "grainy" to high density, and it alone re-characterises
the space from hall-like to plate-like **[DOC]**. Pre-delay 0–990 ms **[DOC]**, with the
manual's own guidance that under 30 ms integrates and 50 ms+ separates **[DOC]**.

**HW-9 — mid-1980s Japanese digital reverbs.** Two dedicated **early-reflection programs**,
low-density and high-density, plus reflection-pattern flavours (hall/random/plate/reverse)
with room size, liveness, initial delay (up to ~400 ms) and a low-pass **[DOC]**. Cheap,
bright, grainy ER — the 80s "ambience send".

**HW-10 — mid/late-1980s Japanese configurable reverbs.** The later flagship let engineers
*assemble* the chain (ER block, reverb block, delay, chorus, EQ, dynamics) rather than pick
presets; documented ranges include room size 0.2–80 m and reverb time 0.1–99.9 s **[DOC]**,
with an explicit ER-level parameter. The earlier unit added a non-linear gated mode **[DOC]**.

**HW-11 — 1990s/2000s Danish large-format reverb systems.** Their spatial algorithms use
ray-traced early-reflection patterns carrying positional cues, with selectable ER *types*
(e.g. smooth / natural / metallic / fast) and independent ER and tail sections **[DOC]**.
This is the purest "ER as a designed pattern library" model available.

**HW-12 — mid-2000s US boutique processor.** Three engines: early reverberation, late tail,
and a separate low-frequency early engine below ~80 Hz; the early section is deliberately
*dense and diffuse* rather than sparse taps, with a selectable early pattern and level
**[DOC/ANEC, maker interview]**. Closest modern relative of REF-A's philosophy.

**HW-13 — early-1980s US effects processor, room algorithm.** Its **Position** control
rebalances early vs late energy *and* changes their character, i.e. it moves the source
front-to-back rather than just wet/dry **[DOC]**. Small-room realism with natural echo
density growth. Cheap-converter grit is part of the sound **[ANEC]**.

**HW-14 — late-1970s US multitap space processor.** Literally a tap machine: one delay line,
~15 taps summed into the feedback path plus ~8 "audition" taps for output; taps were moved
in ~62 µs jumps without smoothing, giving its signature smear and modulation noise
**[DOC, designer's own account]**. Its room mode maps four taps to ER time/level, reverb
level and size **[DOC]**. Designer's stated weaknesses: spectral smear, modulation noise,
and an inability to sound truly distant because the audition taps pick up dry source as ER
**[DOC]**.

*Dropped:* HW-9's flagship predecessor and two Japanese 1980s/90s units (see key) — their
ER structure is not publicly documented beyond marketing, and they add no character the
above lack.

## 2. Shortlist — five types alongside the REF-A core

Ranked by how much distinct ground each covers.

1. **HW-2 (big hall, shape/spread).** Sonic: wide, slow-blooming, randomised hall that never
   goes metallic. ER: envelope-shaped early *energy* with Shape (contour) + Spread (duration),
   plus a few user pre-echo taps; ER feeds the tail. This is the module's flagship.
2. **HW-8 (pattern-density room).** Sonic: neutral-to-dark general room that morphs hall↔plate.
   ER: one continuous density control 0–9 over a diffused cluster, very long pre-delay range.
   Cheapest, most teachable ER control we can ship.
3. **HW-5/HW-4 (plate/instant-onset).** Sonic: bright, zero-ER sheen for vocals and snare.
   ER: none by design — pre-delay then immediate dense diffusion, HF decay much shorter than LF.
4. **HW-6 (ambience / non-linear).** Sonic: short, flat, mix-glue ambience plus the gated
   envelope. ER: dense short cluster, 5–200 ms window, envelope-gated rather than tap-mapped.
5. **HW-11 or HW-13 (positional room).** Sonic: realistic small/medium room with front-back
   placement. ER: selectable ray-traced pattern types (HW-11) or a single Position control that
   trades ER against tail and changes both (HW-13). Pick **one**.

**Redundancies.** HW-1 ≈ HW-2 at lower fidelity — fold in as a "vintage/bandwidth" switch, not
a type. HW-3 ≈ HW-2 minus polish; its user reflect-taps are worth stealing as a feature.
HW-4 ≈ HW-5 for our purposes (both no-ER, fast onset) — one type covers both. HW-7 ≈ HW-12:
both are "invisible neutral room"; HW-12 is REF-A's own lineage, so HW-7 adds little.
HW-9 ≈ HW-14: both are sparse discrete-tap ER — implement one tap engine and vary the tap
count/spread. HW-10's value is a *routing* idea (assemble ER + tail + delay), not a type.

## 3. Numeric targets

| # | Figure | Value | Tag | Confidence |
|---|---|---|---|---|
| 1 | HW-8 pre-delay range | 0–990 ms | DOC | High |
| 2 | HW-8 ER density control | Pattern 0–9, low→high density | DOC | High |
| 3 | HW-8 integration threshold guidance | <30 ms integrates, ≥50 ms separates | DOC | High |
| 4 | HW-6 ER time span | ~5 ms (small room) → ~200 ms (large hall) | DOC | High |
| 5 | HW-9 initial/ER delay max | ~400 ms | DOC | Medium |
| 6 | HW-9 ER densities | 2 programs: low-density, high-density | DOC | High |
| 7 | HW-10 room size range | 0.2–80 m | DOC | Medium |
| 8 | HW-10 reverb time range | 0.1–99.9 s | DOC | Medium |
| 9 | HW-5 decay range | <1 s → ~6 s | DOC | High |
| 10 | HW-5 HF decay at 10 kHz | <1 s at any damper setting | MEAS | Medium |
| 11 | HW-5 usable band guidance | roll off below ~600 Hz, above ~10 kHz | ANEC | Medium |
| 12 | HW-4 tank delay times | ~80–120 ms per chain, 3 all-pass diffusers, 4 outputs | ANEC (patent for topology: DOC) | Low–Med |
| 13 | HW-14 tap counts | ~15 feedback taps + ~8 output taps, one delay line | DOC | Medium |
| 14 | HW-14 tap-move quantum | ~62 µs, unsmoothed | DOC | Medium |
| 15 | HW-12 engine split | 3 engines: early, late, early-LF below ~80 Hz | DOC/ANEC | Medium |
| 16 | HW-7 conversion | 16-bit with 2×/4× oversampling, wide internal word | DOC | Medium |
| 17 | Design rule: distance cue | strong early energy out to ~50 ms past source end | DOC | High |
| 18 | Design rule: ER hazard | lateral reflections ~25–40 ms can reduce spatial impression | DOC | High |
| 19 | REF-A ER spacings | 7 spacing variations, usually size-linked | DOC | High |
| 20 | REF-A LF damping knee | 16 Hz–1600 Hz | DOC | High |

## 4. Not verified — flag before implementing

- HW-2 numeric ranges for Shape, Spread, Size and pre-delay (units and end-stops) — the
  controls are documented, the *numbers* were not confirmed from the manual. **Blocking for
  type 1.** Needs the operating manual PDF.
- HW-1 converter word length, sample rate and bandwidth — repeated in press, no primary spec seen.
- HW-6 sample rate / bandwidth figure — widely quoted, not confirmed here from the manual.
- HW-11 ER type list is confirmed for one surround algorithm only; per-type tap counts and
  time spans are undocumented publicly.
- HW-12 internal structure beyond the three-engine split; no patent or spec confirms tap
  counts or ER window.
- HW-13 designer attribution could not be confirmed; ER tap count undocumented.
- HW-7 algorithm is described only in maker prose ("resonance not reflections"); no patent
  or paper located, so treat the modal claim as marketing until verified.
- No primary ER documentation found for the dropped Japanese units.

## 5. Sources and key

| Label | Device | Source |
|---|---|---|
| REF-A | Waves Renaissance Reverb (R-Verb) | https://assets.wavescdn.com/pdf/plugins/renaissance-reverb.pdf |
| REF-B | Valhalla VintageVerb / ValhallaRoom (Sean Costello) | https://valhalladsp.com/2011/05/04/valhallaroom-early-reflections-versus-early-energy/ |
| HW-1 | Lexicon 224 / 224XL | https://www.liquidsonics.com/2021/07/28/the-big-six-reverb-types/ |
| HW-2 | Lexicon 480L (Random Hall, Ambience) | https://help.uaudio.com/hc/en-us/articles/33194625601044-Lexicon-480L-Digital-Reverb-and-Effects-Manual |
| HW-3 | Lexicon PCM 60 / 70 / 80 / 90 | https://lexiconpro.com/en-US/product_documents/pcm70-ompdf |
| HW-4 | EMT 250 / 251 (Blesser & Bäder, US 4,181,820) | https://www.mixonline.com/technology/1976-emt-model-250-digital-reverb-377973 |
| HW-5 | EMT 140 plate | https://help.uaudio.com/hc/en-us/articles/33030978351892-EMT-140-Plate-Reverb-Manual |
| HW-6 | AMS RMX16 (Ambience, NonLin2, Plate) | https://media.uaudio.com/support/manuals/dd/AMS%20RMX16%20Expanded%20Manual.pdf |
| HW-7 | Quantec QRS Room Simulator (Buchleitner) | https://www.soundonsound.com/reviews/quantec-2496-yardstick |
| HW-8 | Klark Teknik DN780 | https://archive.org/details/Klark_Teknik_DN780_User_Manual |
| HW-9 | Yamaha REV1 / REV7 / SPX90 | https://theatrecrafts.com/archive/documents/yamahaspx90_manual.pdf |
| HW-10 | Roland SRV-2000 / R-880 | https://support.roland.com/hc/en-us/articles/201920819-R-880-Technical-Specifications |
| HW-11 | TC Electronic M5000 / System 6000 (VSS) | https://www.soundonsound.com/reviews/tc-electronic-reverb-6000 |
| HW-12 | Bricasti M7 | https://www.bricasti.com/images/M7.pdf |
| HW-13 | Eventide SP2016 / 2016 Stereo Room | https://www.eventideaudio.com/plug-ins/2016-stereo-room/ |
| HW-14 | Ursa Major Space Station SST-282 (Christopher Moore) | https://valhalladsp.com/2010/05/14/stability-through-time-variation-ursa-major-space-station/ |
| — | Dropped: Sony DRE-2000 / DPS-V77 | no primary ER documentation located |
| — | Griesinger on early energy and spaciousness | https://www.soundonsound.com/people/david-griesinger-lexicon-creating-reverb-algorithms-surround-sound |
| — | Griesinger papers index | https://www.davidgriesinger.com/ |

Short quotations above are paraphrased except two attributed fragments: the DN780 manual's
description of low-density reflections as "grainy", and ValhallaRoom's term "Early Energy".
