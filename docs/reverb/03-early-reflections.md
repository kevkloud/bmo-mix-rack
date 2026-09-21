# 03 — Early Reflections

ER dossier. Reference voicing: a widely used late-1990s native reverb plugin and
its stablemate room-emulator; product names appear only in **Sources**.
**ANECDOTAL** = attributed practice, **DOCUMENTED** = published research or
vendor docs, **UNVERIFIED** = not established.

---

## Part 1 — What mix engineers do

### Jaycen Joshua

**ANECDOTAL — verified, his own words.** In a 2010 interview he is sparing with
reverb: it can "cloud up your tracks and make them less clean", and "reverb is
the kiss of death on rap vocals". He preferred delays for transparency. [S1]

**ANECDOTAL — verified, workshop Q&A attributed to him.**

1. **Pre-delay is a separate delay, not the reverb's own.** He puts a delay 100%
   wet, zero feedback *ahead* of the reverb, because internal pre-delay with a
   wet mix under 100% sums a delayed copy against the dry path and phases. [S2]
2. **Reverbs stack for a purpose, not for "space".** "We'll stack multiple
   reverbs at a time depending on what the record calls for" — including a dark
   short reverb used for warmth and low end rather than size. [S3]

**ANECDOTAL — second-hand only.** The repeated "front-to-back panner": the
room-emulator on an insert, tail killed (decay minimum, reverb level −∞), leaving
only ER; Distance then moves the source front-to-back without wash, ER level
raised to push back and lowered to pull forward. Consistent with his verified
philosophy, and a video exists on the workshop provider's own channel, but every
written account traces to unlinked blog retellings of each other, and the quoted
numbers (distance ≈ 10 m, size ≈ 6000) are **UNVERIFIED**. [S4]

**UNVERIFIED — nothing found** on his ER pre-delay, EQ, width, or ER-to-tail
ratio.

### Others

- **Andrew Scheps (2016), verified.** Layers a slap "somewhere in the 110 ms
  range" with "a very short reverb — either a plate or a room"; the result still
  reads dry. [S5]
- **John Leckie, verified.** A small-room algorithm around 40 ms makes a source
  *drier*: "You can actually make something drier by adding something." [S6]
- **Second-hand:** Chris Lord-Alge extends ambience already in the indirect mics
  rather than manufacturing it, and mixes largely in mono [S7]; Al Schmitt
  blended real chambers and room mics with close mics [S8]; Michael Brauer notes
  "dry" requests really mean "not reverb" [S9].

Common thread: ER serves *placement and tone*, not reverb.

## Part 2 — The science (DOCUMENTED)

**Precedence / Haas.** Below ~1 ms a reflection fuses and pulls the image; from
~1 ms to the echo threshold the direct sound sets direction while the reflection
adds loudness and timbre. Haas (1951): a reflection can be roughly 10 dB *louder*
than direct before dominating, for speech around 10–30 ms. The echo threshold is
signal-dependent — a few ms for clicks, ~30–50 ms for speech, often 50–100 ms for
slow music. Edges are medium confidence; the ordering is robust.

**Detection thresholds.** Olive & Toole (1989): a single lateral reflection is
detectable roughly 15–20 dB below direct for speech at 10–20 ms anechoically. ER
taps are audible far below the level at which they look significant. [S10]

**Initial time delay gap.** Beranek's ITDG — direct sound to first strong
reflection — is his correlate of *intimacy*. Intimate halls have short gaps
(under ~20–25 ms); a long gap reads as a large space. The strongest room-size cue
the ER section owns.

**Lateral energy and width.** Barron & Marshall (1981): apparent source width
tracks the early lateral energy fraction — lateral energy in 5–80 ms over total
in 0–80 ms, standardised in ISO 3382-1, ~0.10–0.25 preferred for concert music.
The binaural correlate is IACC, split early (0–80 ms, ASW) and late (80–1000 ms,
envelopment). Lateral early energy widens; frontal does not. [S11]

**Griesinger.** Strong early reflections randomise the phase relationships
between harmonics the ear uses to judge *proximity* and separate streams, so
energy in roughly 10–50 ms costs clarity and near-ness even while adding
loudness; spaciousness comes from *late* lateral energy beyond ~50–80 ms. ER buys
placement and size but spends clarity. [S12]

**Clarity indices.** ISO 3382-1 defines C50/C80 as the dB ratio of energy before
50 ms (speech) or 80 ms (music) to energy after, and D50 as the same split as a
fraction. Speech rooms want C50 above ~0 dB and D50 ≥ 0.50; music typically −2 to
+2 dB C80. **ER taps land on the numerator** — raising *measured* clarity while,
per Griesinger, subjectively degrading it when taps are few and strong.

**Comb filtering.** One delayed copy at delay *t* gives notches spaced 1/*t* Hz:
1 ms → 1 kHz spacing, 10 ms → 100 Hz. Ripple depth follows relative level — a tap
at −10 dB yields roughly 6 dB peak-to-trough; at equal level the nulls are total.
A few strong taps at similar spacings sound boxy or metallic because the notch
pattern is regular and static. Mitigations: irregular, mutually incommensurate
tap times; higher density; per-tap lowpass; and diffusion, which smears discrete
taps.

**Distance.** Direct-to-reverberant ratio is the primary distance cue; intensity
follows 1/r (−6 dB per doubling) and HF air absorption adds a distance-dependent
lowpass. Raising ER relative to direct lowers DRR and reads as farther — the
Part 1 mechanism.

**Density and mono.** Echo density grows with the square of time; Schroeder's
rule of thumb is ~1000 echoes/s before flutter stops being audible, and mixing
time is commonly estimated as √V ms for volume V in m³ — order 20 ms for a live
room, 100 ms+ for a hall. ER must hand over to the tail by then. A decorrelated
lateral ER pattern is also the signal that cancels on summing, collapsing into
comb filtering rather than silence.

---

## Part 3 — Design requirements

The ER section must be a **first-class, independently controllable stage**.

- **ER level independent of tail level** — the basis of every technique in
  Part 1. Two separate faders, and the ER-only state (tail at −∞) must sound
  good, not degenerate.
- **ER-to-tail crossfeed.** The reference's Diffusion control sets how much
  direct versus ER energy feeds the tail — what makes the tail belong to the same
  room.
- **Pre-delay that moves the tail but optionally not the ER.** Default tail-only,
  so ITDG and tail onset stay separable; add a link switch and support
  **negative** values. Per Joshua's phasing trap, internal pre-delay must live on
  a fully wet path — dry must never be summed against a delayed copy of itself.
- **Size scaling of tap times**, in metres or m³, offering both the linked
  "distance" behaviour (ER spacing, pre-delay and levels together) and unlinked
  size.
- **Pattern / room shape.** The reference abstracts this as a "dimension" control
  from 1 to 4; a shape list (small room, chamber, live room, hall) reads better
  to mix engineers.
- **ER width / decorrelation**, continuous, with a conservative default and a
  mono-sum meter.
- **ER damping / EQ**, per-tap or post-ER; a dedicated ER high-cut is the main
  anti-boxiness tool.
- **Variable density / diffusion**, from a few discrete slaps to a dense smear.
- **Tap gains rolling off as 1/r** from modelled path length, with a per-tap
  lowpass standing in for air and surface absorption.

**Failure modes and tests.**

- *Combing on sustained mono sources* — pink noise and a held vocal note, ER
  only; look for regular ripple on a high-resolution spectrum.
- *Flamming on transients* — dry snare, ER solo, listen for doubling. Culprit is
  any tap strong and late enough (beyond ~25–30 ms) to cross the echo threshold.
- *Smearing consonants* — spoken word, ER only; blurred sibilants mean too much
  10–50 ms energy.
- *Mono collapse* — sum to mono; loss beyond a few dB or new notches means
  decorrelation is too aggressive.

### Target figures

| Parameter | Target | Confidence |
|---|---|---|
| ER time window | 5–100 ms | High (vendor doc) |
| Tap count | ~20 min, variable to dense | Medium |
| Tap level roll-off | 1/r plus per-tap LPF | High (physics) |
| Pre-delay range | −50 to +250 ms, tail-only default | Medium |
| Matching decay | 0.2 s to 5–6 s | High (vendor doc) |
| Decorrelation steps | ≥7, conservative default | Medium |
| HF damping ratio | 0.10×–2.00×, default 0.25×–0.5× | High (vendor doc) |
| LF damping knee | 16 Hz–1.6 kHz | High (vendor doc) |
| Mono-sum loss budget | ≤3 dB at default width | Low (our target) |
| Lateral early energy | 0.10–0.25 | High (ISO 3382-1) |
| ITDG for "intimate" | <20–25 ms | Medium (Beranek) |
| Handover to tail | ≤ √V ms | Medium |

---

## Sources

- [S1] Tingen, "Secrets Of The Mix Engineers: Jaycen Joshua", *Sound On Sound*,
  Aug 2010 — soundonsound.com/techniques/secrets-mix-engineers-jaycen-joshua
- [S2] community.mwtm.com/t/reverb-delay-trick/2380
- [S3] community.mwtm.com/t/lead-vocal-reverb/7296
- [S4] Second-hand accounts of the Waves TrueVerb technique:
  braylenhope.com/jaycen-joshuas-reverb-trick-for-creating-depth/ ·
  audiospectra.net/jaycen-joshua-trueverb-trick/ ·
  mixinggpt.com/blog/jaycen-joshua-mixing-techniques ·
  youtube.com/watch?v=jVfIeUOFntU
- [S5] Levine, "Andrew Scheps: Mixing in Parallel (Part 2)", *Audiofanzine*,
  9 Jun 2016 — en.audiofanzine.com/sound-technique/editorial/articles/mixing-in-parallel-part-2.html
- [S6] Senior, "How To Use Reverb Like A Pro: Part 2", *Sound On Sound*, Aug 2008
  — soundonsound.com/techniques/how-use-reverb-pro-part-2?page=3 (Leckie on the
  Lexicon 480 Small Room algorithm)
- [S7] v2.puremix.com/blog/when-indirect-is-better.html (HTTP 522; summary only)
- [S8] prosoundweb.com/in-the-studio-an-interview-with-legendary-engineer-al-schmitt/
- [S9] uaudio.com/blogs/ua/michael-brauer-ua-interview
- [S10] Olive & Toole, "The Detection of Reflections in Typical Rooms", *JAES*,
  Jul 1989 (pearl-hifi.com mirror; PDF not machine-extractable — figures from
  general knowledge, medium confidence)
- [S11] acousplan.com/glossary/lf-lateral-fraction
- [S12] davidgriesinger.com — esp. "The Effects of Early Reflections on
  Proximity, Localization and Loudness"
- [S13] Waves TrueVerb owner's manual —
  archive.org/stream/Waves_TrueVerb_owners_manual/Waves_TrueVerb_owners_manual_djvu.txt
- [S14] Waves Renaissance Reverb user guide —
  assets.wavescdn.com/pdf/plugins/renaissance-reverb.pdf
