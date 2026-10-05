# Reverb groundwork 01 — reference software

Sonic references for the reverb module. Tags: **[D]** documented in a primary
source, **[M]** measured, **[A]** anecdotal (review/tutorial/marketing). No
independent measurements of either product were found anywhere, so **[M] is
unused below** — that is itself a finding. Brand names are not in the
repository; the key from labels to products is kept outside it.

- **Reference A** — a widely used late-1990s native algorithmic reverb plugin.
  Primary reference.
- **Reference B** — a popular algorithmic "vintage digital" reverb plugin whose
  developer blogs about his algorithms. Second reference.
- **Reference C** — a sibling room reverb by the same developer, cited only for
  his written position on early energy.

## A. Reference A

**Architecture [D].** Descends from the same maker's earlier room-simulation
plugin: that product's early-reflection system was taken as a starting point and
improved, and a **new, separate tail engine** was built behind it. Two
generators, each with its own output fader.

**Types (12) [D].** Numbered here in the maker's menu order, not named: types 1
and 2 are halls, 3 a room, 4 a chamber, 5 a church, 6 and 7 plates, 8 reverse,
9 gated, 10 non-linear, 11 an echo-and-reverb hybrid and 12 a resonant reverb.
The type menu sets reverb **and
early-reflection behaviour globally** — it swaps the engine. Per-type reflection
patterns are not published.

**Reverb properties [D]**
- **Pre-delay −160…+160 ms**, default 0. Defined as the offset between the dry
  signal *plus the early reflections* and the **onset of the tail**: ER travel
  with the dry signal, pre-delay moves only the tail. Negative values delay the
  **dry** signal instead (control turns red), giving a pre-onset bloom.
- **Time (RT60) 0.1…20.0 s**; vendor page: 100 ms to 20 s.
- **Size 1…100 %** — per-type; explicitly includes **ER spacing** and tail
  dimension.
- **Diffusion 1…100 %** — not a density knob: a **balance between the direct
  signal and the ER feeding the tail**. At 0 % the direct signal feeds the tail,
  at 100 % only the ER do.
- **Decay 0.04…3.5 (3.5 = linear)** — truncates the tail, making any type gated
  or non-linear. Manual suggests RT ≥ 1 s, ~3 s as a start.

**Levels [D].** Early Reflections **0…−40 dB (off)**; Reverb tail **0…−40 dB
(off)**; Wet/Dry **0…100 %** (wet = ER + tail); Output **−24…0 dB**. The ER/tail
balance is two independent trims, not a morph.

**Decorrelation [D].** Stepped, **Variation 0 (least)…6 (greatest)** — seven
positions — setting L/R correlation **within the early reflections only**;
called subtle but important for colour. Vendor marketing describes the same
seven as ER **spacing** variations: closer = centred image, wider = wider
image [A].

**Damping — frequency-dependent decay [D].** Low knee **16…1600 Hz** with low
ratio **0.10…2.00**; high knee **1000…2100 Hz** with high ratio **0.10…2.00**.
Ratios under 1.00 shorten that band's decay. Note the narrow high knee range:
this is a decay multiplier crossover, not tone.

**Reverb EQ, separate [D].** Low shelf knee 16…1600 Hz, high shelf knee
1000…2100 Hz, each **−24…+12 dB** (−24 dB turns the shelf into a cut). Stated to
sit **pre** the early reflections and reverb, so it shapes what feeds both.

**Latency/CPU.** **0 samples at 44.1–48 kHz and 88.2–96 kHz** native (3 on one
legacy DSP platform) [D]; 24-bit/96 kHz [D]. A period magazine review put it at
one instance per DSP chip [A]; no modern native CPU figure found.

**Reputation [A].** Smooth, dense, "vintage"; sits in a mix rather than on top;
a staple on lead vocals (the vendor quotes a mix engineer calling it his main
reverb on a named pop vocal). The magazine review was cooler: better than most
native reverbs of its era, but the early reflections "can sometimes sound a
little ragged", with occasional mild mid-range ringing and no ambience
algorithm. Vendor's suggested work order [D]: type → properties → ER/tail
balance → EQ and damping → decorrelation.

## B. Reference B

**Modes (22), from the developer's own descriptions [D]** (the modes' own names
are left out; each group keeps its count and what each mode does)
- *Hall/plate/room lineage (late 70s–early 80s), five modes*: a large hall (huge
  image, echo density adjustable sparse→dense, chorused modulation, clean tone);
  a brighter hall (brighter onset, deeper modulation); a plate (highly diffuse,
  bright onset, high density); a room (medium diffusion/early echo density,
  darker); a chamber (transparent, dense, less colour).
- *Late-80s randomised lineage, three modes*: a deep, wide space with a **slow
  attack** and more diffusion than its inspiration, using randomised internal
  delays rather than chorusing (avoiding metallic artefacts without pitch
  change); the same space chorused instead; and the same randomisation with a
  **tighter attack**.
- *Explicit ER modes, two*: an **ambience mode** — time-varying **randomised
  early reflections** plus a full tail, with the **Attack knob as the
  early/late balance**, for air felt not heard — and a mode that nods to a
  classic German 1970s digital reverberator (HW-4): **discrete early
  reflections** then a late reverb that builds echo density rapidly, plus
  period converter bit reduction and floating-point gain.
- *Converter grit, two modes (a hall and a plate)*: emulating a famous 1980s
  hall unit (HW-1) and a British 1980s unit (HW-6): floating-point converter
  quantisation, steep anti-alias lowpass, fixed-point quantisation of audio
  **and of the modulation signals**.
- *Two smooth modes (a plate and a room)*: modern update of the classic
  hall-unit algorithms, with output taps shaped for natural exponential decay.
- *Others, eight modes*: a non-linear mode (Size = duration, Attack morphs
  truncated → flat gated → reverse); three tape-coloured modes (a hall, a
  chamber and a neutral one: wow-and-flutter plus pre-emphasis/nonlinearity/
  de-emphasis saturation); a cathedral; a 1980s "room simulator" style mode
  with higher density, whose late diffusion is described as **reducing
  perceived pre-delay**; and one further chamber and one further hall.

**Colours [D].** *1970s*: ~10 kHz bandwidth, internally **downsampled** for
lower-rate artefacts, dark noisy modulation that can throw random sidebands on
sustained notes (intentional). *1980s*: full bandwidth and rate, brighter, still
noisy modulation but different artefacts. *Now*: full bandwidth, clean
colourless modulation.

**Controls.** Mix, PreDelay, Decay (0.2…70 s [A]), Size, Attack, Early
Diffusion, Late Diffusion, Mod Rate, Mod Depth, High Cut, Low Cut, high-shelf
Damping (100 Hz…20 kHz [A]), Bass multiplier **0.25×…4×** of decay below a
crossover [A] with **Bass Freq 100 Hz…10 kHz** ([D] — vendor changelog restores
exactly this range). **Attack** builds the onset in most modes and becomes the
**early/late balance** in the two explicit-ER modes [D]. **Early Diffusion**
controls both the **level and density of the early reflections**; **Late
Diffusion** raises tail density and masks pre-delay [A, from tooltips].

**On early reflections [D].** Writing about Reference C, the developer argues
tapped-delay ER (a few dozen ray-traced taps) miss real surface diffusion and
room complexity; that makers bolt allpasses on to raise density, causing
metallic artefacts on vocals and drums; and that most "Diffusion" controls exist
to manage those artefacts. Reference C's early section has **no allpass delays
at all**, aiming instead at early energy that is dense, colourless and spacious
— an "impossible room" — with Early Size (10–50 ms typical for small spaces,
longer for halls) and Early Cross for L/R crossfeed. More allpasses slow the
onset unnaturally; high allpass coefficients speed it and raise density but turn
metallic. Psychoacoustic design, not geometric — the opposite pole from
Reference A.

## C. Comparison

| Aspect | Reference A | Reference B |
|---|---|---|
| Architecture | ER generator + separate tail engine [D] | Unified per-mode algorithms; a distinct ER stage only in two modes [D] |
| Top-level choice | 12 types; type swaps ER pattern and tail together [D] | 22 modes + 3 colour eras [D] |
| ER/tail balance | Two absolute faders, 0…−40 dB [D] | One Attack knob (ER modes); elsewhere Early Diffusion sets ER level and density [D/A] |
| "Diffusion" means | Direct-vs-ER feed into the tail [D] | Echo density, split early/late [D/A] |
| Onset | ER pattern, Size (spacing), Diffusion; tail onset shifts ±160 ms [D] | Attack, Size, early/late diffusion; modes characterised as slow vs tight attack [D] |
| Modulation | No control documented [D, by absence] | Rate and Depth exposed; chorused / randomised / chaotic per mode; colour era changes its artefacts [D] |
| Damping | Two knees, decay **multipliers** 0.10–2.00, plus separate pre-generator shelving EQ [D] | High-shelf damping plus bass **multiplier** 0.25×–4× over a crossover, plus cuts [D/A] |
| Stereo | Seven ER decorrelation variations [D] | Width from mode and modulation; no ER decorrelation control [D, by absence] |
| Latency | 0 samples native [D] | Unpublished; no lookahead described — unverified |

Both agree on: pre-delay ahead of the tail, a Size that scales the whole space,
frequency-dependent decay as a *multiplier* rather than a filter, and the first
~50 ms deciding how big and how close the space reads.

## D. What to take — behaviours to match

1. **Two generators, two faders**: ER and tail get independent absolute levels
   (0…−40 dB, −40 = off) with one wet/dry over the sum. No single morph knob.
2. **Pre-delay moves the tail, not the ER.** Dry and ER stay together; support
   negative pre-delay (delaying dry) with a visible warning; match ±160 ms.
3. **Type selects an ER pattern.** Switching type may be a large jump in sound.
4. **Size scales ER spacing first** — perceived distance and width must move,
   not only tail length.
5. **Stepped ER decorrelation, about seven positions**, near-mono to widest,
   acting on the ER stage only.
6. **Diffusion as a feed balance** (direct vs ER into the tail). Add a B-style
   density control only if it stays ring-free at maximum.
7. **No allpass ringing in the ER stage** — dense early energy without short
   allpasses, so vocals and drums never go metallic.
8. **Damping as decay multipliers; EQ as tone; visibly separate.** Two knees at
   0.10–2.00, plus shelving EQ ahead of both generators. This pairing is much of
   the reference darkness.
9. **One decay/truncation control** turns any type gated or non-linear at long
   RT — no separate gated and reverse algorithms.
10. **Zero added latency**, native, at every supported rate.
11. **From Reference B only**: an onset control morphing early→late balance;
    optional era bandwidth limiting; modulation as rate/depth, defaulting to
    randomised rather than chorused to avoid pitch wobble.

## Could not verify

- Per-type ER tap patterns/counts for Reference A — unpublished.
- Whether Reference A's "7 spacing variations" (vendor page) and "decorrelation
  Variation 0–6" (manual) are one control described twice. Almost certainly, but
  unstated.
- Reference A's modern native CPU cost.
- Reference B's exact ranges for PreDelay, Decay, Size, Mod Rate/Depth and cuts:
  no manual is published (tooltips only); ranges tagged [A] come from tutorials,
  except Bass Freq.
- Any impulse-response, echo-density or latency measurement of either product.

## Sources

The key from these labels to products lives outside the repository.

| Label | Sources |
|---|---|
| Reference A | the maker's user guide (and a mirror of it), product page and plugin-latency table; a magazine review |
| A's predecessor | named in the Reference A manual introduction |
| Reference B | the maker's product page and a developer blog post on its modes; ranges from a third-party quick-start guide |
| Reference C | two developer blog posts, on early reflections versus early energy and on diffusion allpasses and metallic artefacts |
| "classic German 1970s digital reverberator" (HW-4) | named generically by the developer of References B and C |
| "famous 1980s hall unit" (HW-1) | named by the developer of References B and C |
| "British 1980s digital reverb" (HW-6) | named by the developer of References B and C |

Developer of References B and C: Developer A (Developer B credited on the converter-grit
modes).
