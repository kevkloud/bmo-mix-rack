# 10 — DSP spec (BMO reverb, plugin code `Brvb`)

Math and prose only. Traces to `00`–`05`. **CALIBRATE** marks anything that must
be fitted by measurement and ear. Where `03` and `05` disagree, **`05` wins** —
it is the primary-source dossier, and several figures carried from `03` are
folklore by its account. Those corrections are called out in place.

## 0. Design thesis

**Reference B's sound with Reference A's functionality.** The owner likes the
ER/tail separation, so the *control structure* is Reference A's — two generators,
two absolute faders, pre-delay on the tail only, a "what feeds the tail" control,
Size scaling ER spacing, damping as decay-time multipliers, stepped ER
decorrelation. The *tail character* is Reference B's — dense, lush, randomly
modulated, with a smooth diffuse onset and a contour that lets it bloom behind
the ER rather than arriving as a second event. The ER section is where the two
references disagree, and §3 resolves that with one control rather than a side.
This is "in the spirit of", not a reproduction: no IR is matched, and §8 lists
what about B's sound is simply not public.

## 1. Architecture

**Late network: 8-line FDN, Householder matrix, per-line absorbent filters**
(shortlist **A**, `04` §5). **ER: image-source tap tables into a feed-forward
multichannel mixing diffuser** — no recursive allpass anywhere in the ER path.

Only the Jot absorbent-filter formulation derives Reference A's 0.10–2.00× decay
multipliers from a target T60(f) instead of tuning toward it (`04` §1), and that
pair is "much of the reference darkness" (`01` D8). The figure-of-eight tank is
plate-leaning and weak at hall scale and per-band T60 (`04` §4), so it would
force a second topology for the halls; the nested-allpass loop has the least
accurate decay control, the one thing Reference A is precise about. The FDN also
keeps delay lengths independent, so type and Size are table changes over one
topology, and it alone can be made pitch-artefact-free outright (`04` §3).

Reference B/C's objection is not to taps: it is to **allpasses bolted on behind
taps** to raise density, which is what goes metallic on vocals and drums (`01` B,
D7). So the density stage is feed-forward — parallel short delays recombined
through a 4×4 orthogonal butterfly, repeated. Finite impulse response, no poles:
it cannot ring, and has no coefficient that "speeds the onset and turns
metallic". That answers the owner's hardest constraint, and is why ER can solo.

**Not done in v1:** convolution or hybrid ER (fails zero reported latency);
scattering delay networks (a simulation, not an effect); per-tap multi-band
shelving (cost, §6); freeze; ducking; tempo-synced pre-delay (no host plumbing
exists, `00` §2); user-editable tap lists; separate gated and reverse algorithms
(Decay truncation covers them, `01` D9 — **though note that since the trim the
truncation is a per-type constant and every one of the six types ships it
linear, i.e. off, so nothing in v1 actually reaches a gated decay; a gated type
or a returned knob is what would**); **negative pre-delay** (§2).

### Types

A type changes **only constants** over the shared topology: ER tap table, ER
window and default density, the eight FDN times, input-diffusion depth, damping
and modulation defaults, input bandwidth, default ER→tail feed, and a reserved
**era block** (below). **Since the 2026-09-21 control-set trim (§6, `11` §4a)
the block also carries the two damping knee frequencies, the tail-onset contour
(Attack), the decay-truncation shape and the early cluster's rise exponent *p*,
none of which is a user control any more**, plus the two generator levels, which
became per-type the same day so that Ambience could be built as specified. Which
half of the block a field is in — the nine with a host lane, the rest without —
is `modules/reverb/params.h`'s `TypeConstants` and `11` §4b; it changes nothing
here, because a type was always a table of constants and these are more of them.
No audio-path branch beyond a table lookup. Buffers are
sized in `prepare()` for the largest type (`00` §2), so switching never
allocates.

The list is append-only once shipped, so this order is final.
**v1 (6):** 1 Room · 2 Chamber · 3 Hall · 4 Cavern · 5 Plate · 6 Ambience —
the Reference-A core set, small→large→plate→ambience. Ambience is the ER-star
type: tiny tail, ER-dominant, where "tail off, distance sets depth" lands by
default. **Appended later:** 7 Shaped Hall · 8 Pattern Room · 9 Positional
Room · 10 Vintage Room.

*Church was struck from this reserve on 2026-09-21*, because Cavern at index 4
carries exactly that character and the list would otherwise hold a type that
already ships (`11` §1). Note too that the four remaining read as universal
controls rather than as rooms, so the reserve is emptier than it looks — and
appending is not free. The **count** is what `juce::AudioParameterChoice`
normalises by, so a seventh type remaps every recorded automation lane on TYPE.
Sessions and presets survive it, because state is stored as plain values keyed
by id; automation does not.

HW-2 is `02`'s flagship, but its distinguishing feature is an ER *envelope*,
which belongs to every type as a mode (§3). HW-8's pattern knob likewise becomes
the universal Density control. Plate with the ER fader at −40 dB covers
HW-5/HW-4; HW-6's non-linearity is the Decay control.

### Era / colour voicing — verdict

Reference B's three colour eras (bandwidth, modulation style, converter grit,
`01` B) are a signature of its sound, and the rack has a precedent for a stepped
voicing switch over one topology in the FET compressor. **It does not earn a
permanent parameter in v1.** Era × type multiplies the tuning surface by three
before one type has been voiced, and the grit itself (quantisation curves,
anti-alias filter order, modulation-signal quantisation) is described but never
specified publicly, so it would ship as guesswork. Instead each type's constant
block **reserves three era fields** — bandwidth, modulation distribution
(clean-random / noisy-random / chorused), and an output quantisation depth left
at "off" — set per type in v1 (Plate and Chamber bright and clean, Room and
Ambience narrower, Cavern deep-random). Because they already exist as
constants, promoting them to a 3-position Era control in v2 changes no type
ordinals and no state layout. Anything added there aliases at 44.1 kHz and must
be bandlimited or oversampled (`04` §3).

## 2. Signal flow

**Input.** Stereo→stereo is native; mono→mono works; **mono→stereo was wanted
and impossible when this was written, and shipped on 2026-09-21** in
`core/product/BusLayouts.h` — the rack widens once at its own input, ahead of
slot 1, so no slot ever sees an asymmetric layout (`11` §2b). The reverb sees
the mono input duplicated into both channels and is free to decorrelate its tail
from it. Fixed 20 Hz high-pass plus an input high-cut
2–20 kHz. The **Reverb EQ** sits here, because Reference A's EQ is *pre* both
generators (`01` A) — and *where* it sits is the sentence of this paragraph that
survives every change to *what* it is. It is a **three-node parametric** since
2026-09-21: a low shelf 16–1600 Hz, a bell 20 Hz–20 kHz and a high shelf
1 kHz–20 kHz, each −24…+12 dB with its own Q, plus a four-position FILTER mode
(`Off` / `Lo Cut` / `Hi Cut` / `Bandpass`) that turns the two outer nodes into
cuts. **It was two shelves, with the high one spanning 1000–2100 Hz** — 1.07
octaves, a high shelf that could not reach air — which is Reference A's own
figure and was inherited rather than chosen; `11` §4c carries both changes and
`11` §4 is the schema. The shape rule is that a cut keeps its corner and its
resonance and **has no gain**, so GAIN stops reaching a node the mode has made a
cut. Input diffusion (2 allpasses per channel, 4 for Plate) is tail-path only.

**Pre-delay** is **tail-only and wet-path-only**, 0…+250 ms. Dry is never delayed
nor summed against a delayed copy of itself — the phasing trap behind the "100 %
wet delay ahead of the reverb" habit (`03` Part 1); honouring it removes the
reason for the habit. **ER travel with dry** (`01` A). That was an ER Pre-delay
Link switch until 2026-09-21; it is now `kPreLinkFixed = false` for every type —
off is the reference behaviour, every type shipped it identically and nobody
automates it (§6, `11` §4a). The behaviour is unchanged; only the switch is
gone, and restoring it is an append if a listening pass ever wants it.

**Negative pre-delay, honestly.** Reference A's −160 ms delays the *dry* signal.
Here that delays the module's whole output against the host timeline, which is
latency, reported through `latencyForParams` and pushed on change (`00` §2) — so
an automatable negative pre-delay would thrash PDC on every move. Dimension
reports zero precisely because "nothing the host receives is a delayed copy of
what it sent" (`modules/dim/dsp/DimDsp.h:45-58`); negative pre-delay breaks that
condition. **v1 is 0…+250 ms**; if the pre-onset bloom is wanted later it ships
behind an explicit report-latency switch, off by default.

**ER→tail feed** — Reference A's Diffusion, a balance, not a density knob. With
*d* ∈ [0,1] the tail input is (1−*d*)·direct + *d*·ER, tapped from the ER bus
*before* decorrelation and *before* the ER fader, so the two faders stay
independent (`03` Part 3) while the tail still belongs to the room. Default 0.7.

**Tail onset (Attack).** Reference B's contribution to the voicing — **a
per-type constant since 2026-09-21, not a control** (§6, `11` §4a), on the
owner's own words that attack should be type dependent. The acoustics are
unchanged: a 0–1 contour applied as a rising envelope on the FDN *input*,
ramping over 0–120 ms, so the tail blooms behind the ER instead of arriving with
it. At 0 the tail is immediate (plate behaviour) — which is why Plate's constant
is 0 and is the one value in the row that is not a placeholder; at 1 it is a
slow bloom that also raises the perceived ITDG without touching pre-delay. The
panel prints the bloom on its TAIL page because there is no knob to read it off.

**Levels** (`01` D1): ER 0…−40 dB (off), Reverb 0…−40 dB (off), two absolute
trims; wet = ER + tail; Mix 0–100 %; Output −24…0 dB. **Width** is M/S gain on
the tail only; ER width is the stepped decorrelation.

## 3. Early reflections — and the density bridge

**Geometry and gain.** Taps come offline from the image-source method (`04` §2)
for a shoebox of proportions 1 : 1.4 : 1.9, source and listener off-centre,
orders 1–3. For image *k* at path *d*ₖ, order *n*ₖ: time *t*ₖ = *d*ₖ/*c*
(*c* = 343 m/s), gain *a*ₖ = (1 m/*d*ₖ)·β^{*n*ₖ}, pan from the image bearing. No
closed-form tap-gain law is published; **1/t spreading × exponential absorption
is the physically correct model, and it is both, not either** (`05` §10.3).
β = 0.70 Room … 0.88 Cavern (CALIBRATE). Base count **21**; Moorer's 19-tap
table, span 4.3–79.7 ms, is the sanity reference, and its implied direct distance
of ~3.4 m checks the *d*ref choice (`05` §10.2).

**Window.** "Small room 5–30 ms, hall 20–100 ms" is manual folklore with no
source (`05` §10.1, §11), so `03`'s 5–100 ms is *consistent with* the anchors,
not derived from them. The citable anchors are EBU's **15 ms** for control rooms,
ISO's 50/80 ms clarity splits, Polack's √V ms, and arithmetic: a listener 1 m
from a wall gets its reflection at 5.8 ms, 3 m at 17.5 ms.

**Per-tap colour.** Air absorption is weaker over ER distances than assumed — a
34 m path loses only ~2 dB at 8 kHz — so HF loss is mostly **surface**
absorption, scaling with order. `05` §11 warns that second-hand quotations of ISO
9613-1's table are not to be trusted, so coefficients are fitted from **ISO
9613-2 Table 2 octave bands** (not pure-tone values), and every figure here is
CALIBRATE. Cutoff *f*ₖ = 16 kHz · λ^{*n*ₖ} · (1 m/*d*ₖ)^κ, λ ≈ 0.8, κ ≈ 0.2.
Taps share **four** order-banded filters, not 21 (§6). Haas's finding that
HF-rich echoes disturb disproportionately, and that rolling off an echo's highs
raises its critical delay at almost no loudness cost, is the warrant for
filtering taps at all (`05` §1.2).

**Lateral distribution and mono.** Target early lateral fraction **0.10–0.35**,
Barron's recommended range; measured mean across 189 positions ≈0.19, ISO JND
0.05 (`05` §3) — this supersedes `03`'s 0.10–0.25. **Band matters**: the spatial
measures weight 125–1000 Hz while the clarity measures weight 500–2000 Hz, so
lateral spread must be built in the low-mids; broadband early taps target the
wrong band (`05` §3). First one or two reflections stay near centre so the
phantom centre holds (`04` §2). Decorrelation uses **different tap sets per
channel**, never an L/R offset on one set — an offset collapses to combing in
mono (`04` §2).

The mono bound is exact: mono-sum loss is 10·log₁₀((1+γ)/2) dB with γ the ER bus
L/R correlation, and `03`'s 3 dB budget is precisely γ = 0. **Rule: γ ≥ 0 for
Variations 0–5.** There is no published psychoacoustics on ER mono compatibility,
only the algebra (`05` §9.3), so this is an engineering target, not a finding.
Zurek's binaural decoloration gives a dichotic reflection ~10 dB more headroom
before colouring than a diotic one below 5–10 ms (`05` §6.3) — that headroom is
what Variation spends, bought at the cost of mono robustness, a trade `05` notes
has no published resolution. **Variation 6 is built differently**: Schroeder's
1958 complementary-comb pair (L = M + delayed, R = M − delayed) is the one
construction whose transfer functions sum to unity, so the mono sum is exactly
flat (`05` §9.3). It is the widest *and* the only provably uncoloured-in-mono
setting — but the ER vanish in mono entirely, which the panel must label.

**Combing.** *Against dry:* below 100 % wet the ER sum against dry; with
mono-summed ER power *P* relative to dry the RMS ripple ≈ 8.686·√(*P*/2) dB, so
≤1 dB RMS needs *P* ≤ −15.8 dB. The bound is therefore **a function of the
wet/dry setting**, which the DSP cannot fix and the panel must say. *Per tap:*
single-tap ripple is 20·log₁₀((1+*a*)/(1−*a*)), matching `05` §6.1's table
(−10 dB → 5.7 dB, −20 dB → 1.74 dB), so a tap at ≤ −15.3 dB contributes ≤3 dB.

Two corrections to `03`. First, **ripple depth is not the detection bound**:
Brunner et al. detected comb distortion with the copy 13–18 dB down on average
and to **−21.5 dB (piano) and −27 dB (snare)** for individuals, so "reflections
below −20 dB are inaudible" is contradicted (`05` §6.2, §11). Detecting the ER is
wanted; colouring the source is not — −15.3 dB is a *colouration* ceiling, not an
audibility floor. Second, an ERB-spacing rule is the wrong instrument: the
published danger zone is **Δt ≈ 5–20 ms**, the "box-Klangfarbe" window, with
thresholds lowest at 2–5 ms, frontal reflections colouring most near 5 ms and
lateral near 10–20 ms (`05` §6.3). Most small-room taps live there and cannot be
excluded, so the rule is a **level ceiling, not an exclusion**: taps in 2–20 ms
obey Kuttruff's ΔL ≤ −0.6·*t* − 8 dB (*t* in ms) — −14 dB at 10 ms, −20 dB at
20 ms, quoted via Halmrast and known to over-predict past ~30 ms — with the
~10 dB dichotic bonus available once decorrelated. Below 1 ms a tap fuses and is
allowed (`05` §1.1). Spacing rules stand: minimum separation 0.9 ms, no two
inter-tap gaps within 2 % of each other, ±3 % deterministic per-type jitter, and
times **mutually prime / incommensurate**, not prime (`05` §11).

**Flamming, and the Griesinger tension.** `03`'s rule that ≥50 % of ER energy
should fall before 30 ms **is dropped**: Griesinger holds that excess reflections
anywhere in 10–100 ms reduce engagement, that reflections before ~30 ms are rare
in classic orchestral recordings, and that localization needs ~20 ms clear before
significant reflected energy (`05` §4). He is contested — Pätynen and Lokki find
strong early lateral reflections enhance dynamics — but on a lead vocal, the
owner's target material, his is the right caution. Replacing it: (i) no tap after
25 ms exceeds −12 dB relative to cumulative ER energy at 25 ms; (ii) cumulative
energy in successive 5 ms windows is non-increasing after the peak window, so
there is no second onset; (iii) at the default ER fader, summed reflected energy
in his 100 ms analysis window sits **≥3 dB below direct** — the LOC condition for
separating direct from reverberation; (iv) a deliberate small allocation
**inside 5 ms**, because reflections within ~5 ms *increase* proximity and extend
the localization limit while those after ~7 ms decrease both, his crossfade being
centred at 6 ms (`05` §4). `02`'s hazard, isolated lateral reflections at
25–40 ms, is covered by (i) and (ii). ITDG is **not** a design target: Beranek
withdrew the intimacy claim in 2004, it is not an ISO parameter, it ranks last of
his five predictors, and "<20 ms intimate" is unattributed (`05` §2, §11). His
tabulated halls, 15 / 21 / 28 ms, are a plausible range to sit inside, no more.

### The bridge: one Density control

Density sweeps continuously from **discrete positional taps** (Reference-A depth
placement, ER-only use) to **dense shaped early energy** (HW-2's idea,
Reference-B's diffuse onset), with no allpass at either end. It resolves the
disagreement instead of picking a side.

A fixed master sequence of *M* = 48 tap times is built once per type: the 21
image-source taps plus 27 velvet-noise infill times, all pre-jittered, all inside
the window, infill gains drawn from the *same* 1/r·β^order envelope evaluated at
their own times so the contour is identical at every density. Each tap carries an
activation threshold θₖ ∈ [0,1]; the 21 core taps have θ = 0, infill thresholds
spread over (0,1]. At density *D* the weight is a ramp, not a switch:
*w*ₖ(*D*) = clamp((*D* − θₖ)/Δ, 0, 1) with Δ ≈ 0.08. No tap ever appears at
non-zero level, so no click. Energy is held constant by renormalising
ãₖ = *a*ₖ·*w*ₖ·√(*E*/Σⱼ*a*ⱼ²*w*ⱼ²); the denominator is continuous and bounded
away from zero because the core taps never switch off, so ãₖ is continuous in *D*
and there is no level jump. Above *D* ≈ 0.6 the diffuser goes 1 → 2 → 3 stages,
each faded in with the uncorrelated-crossfade normaliser 1/√((1−*w*)²+*w*²), an
orthogonal butterfly being energy-preserving either side of the fade. Infill
pulses are placed **grid-based with jitter** — one per equal window — which
sounds smoother than fully random placement at the same density, and the top end
must clear **≥2000 pulses/s**, or ~600/s if low-passed at 1.5 kHz ("dark velvet
noise", `05` §6.4). Calibrate against Abel & Huang's normalised echo density:
listeners consistently report character changes at **NED 0.3 and 0.7** (`05` §8),
so Density should place those at recognisable knob positions, with NED → 1 at the
end of the window marking handover. One caution against over-claiming: diffuse
reflections are detected at *lower* levels than specular ones of equal energy, so
"diffusion hides reflections" is not supported (`05` §6.4).

Density and the feed control together span both philosophies. Low density with
*d* = 1 gives a **room-shaped** reverb: discrete positional taps, and a tail that
inherits their timing, colour and spacing. High density with *d* = 0 gives a
**unified diffuse** reverb: a smooth early wash and a tail fed from direct, one
continuous space with no articulated pattern. The two extremes are Reference A
and Reference B, and everything between is available.

**ER Mode** remains Taps / Energy / Blend for the envelope itself: Energy mode
replaces image-source times with pure velvet noise enveloped by Shape and Spread
after HW-2. The documented behaviour — Shape 0 builds explosively and decays
quickly, higher Shape builds more slowly and sustains for the time Spread sets —
is confirmed; **the end-stops are not** and stay `02`'s flagged unknown.
Parametrise as a rise (*t*/τ_r)^p to a plateau then exponential handover, Shape
setting *p* ∈ [0,3] and the plateau fraction, Spread setting σ ∈ 5…200 ms
(`02` row 4). All CALIBRATE. **Shape is a per-type constant and not a control**
since 2026-09-21 (§6, `11` §4a): a unitless exponent whose end-stops §7 itself
still marks unconfirmed is character, in the way Attack and Decay Shape are.
*p* is exactly what it was and the envelope is exactly what it was; **ER SPREAD
stays a parameter**, so σ is still on a knob.

**Size.** *t*ₖ(*S*) = *t*ₖ,ref·*S*/*S*ref with *a*ₖ ∝ 1/*d*ₖ and cutoffs
re-derived; scaling times while keeping the pattern preserves the room's identity
(`04` §2). *S* = 0.5–80 m (`02` row 7); window clamped to 5–100 ms
(Room/Chamber/Ambience) or 5–200 ms (halls). **Moving Size crossfades, never
glides**: two tap sets from one buffer, raised-cosine over 30 ms — exactly
`DetuneVoice` (`modules/dim/dsp/DspCore.h:61-139`), whose windows sum to one so a
steady input stays steady. A glide would Doppler every reflection at once, which
on a vocal is a chorus, not a room. Re-triggered when accumulated |Δ*S*| passes
1 %; the late network uses the same scheme, since `04` §3 warns the two drift
apart otherwise.

**Stepped decorrelation.** Seven positions, Variation 0…6 (`01` A, `02` row 19):
per-channel tap permutations plus a lateral-spread scalar, γ falling ≈0.95 →
≈0.05. ER only, default 2. **ER high-cut**: one post-ER shelf, 1–20 kHz, default
7 kHz — the main anti-boxiness tool (`03`), and the fallback if the four band
filters must go.

**ER-only (tail at −40 dB).** Every rule above is specified *in the solo
condition*. The bus is full-band and self-terminating — the last tap ramps out
over ≥5 ms so the cluster does not end on a discontinuity. A Distance macro
raising ER level while applying 1/r and the distance low-pass to the whole set
gives the front-to-back technique directly. Two `05` §7 figures shape it.
Perceived distance follows *r*′ = *k*·*r*^a with **mean a = 0.54 across 84 data
sets, none above 0.9**, so moving apparent distance by *F* needs the physical cue
moved by *F*^{1/0.54} ≈ *F*^{1.85} — the macro's taper must be that expansion or
the control feels inert. And Bronkhorst & Houtgast's DRR uses a **6 ms window for
direct energy**, so everything after 6 ms, all our ER included, counts as
reverberant; no published ER-to-direct ratio exists separate from the tail, so
exposing that split is an engineering choice, not a result. Nothing in the ER
path needs the tail to exist.

## 4. Late reverb math

**Delays.** Eight lines log-spaced over [τ̄/1.3, τ̄·1.3], τ̄ per type (Plate 18,
Ambience 20, Room 25, Chamber 35, Hall 55, Cavern 80 ms; CALIBRATE).
*m*ᵢ = **mutually prime** integers near τᵢ·*f*s — `05` §11 records that the
published requirement is mutual primality or incommensurability, not primality,
and that the classic hardware values were picked with "no mathematical basis" —
recomputed at each rate, not multiplied (`04` §3).

**The mode-density rule scales with decay, and `04`'s statement of it dropped
that.** Schroeder & Logan is 0.15 modes/Hz *at T60 = 1 s*, giving
**Σ*m*ᵢ ≥ 0.15·T60·*f*s** (`05` §8). Eight lines are therefore adequate only up
to a decay: Hall at τ̄ = 55 ms has Σ ≈ 0.44 s and covers T60 ≈ 2.9 s; Plate at
τ̄ = 18 ms has Σ ≈ 0.14 s and covers barely **1 s**. Every type is modally sparse
at long decays, not just Plate. Three ways out, all moving the CPU figure: N = 16
for the long-decay types, larger τ̄, or accept sparsity and let modulation carry
the colour — defensible, since `05` §6.4 records Dattorro rejecting
colourlessness as the goal outright.

**Matrix.** Householder — N adds plus one broadcast, maximally mixing at N = 8,
recomputed from a normalised vector so orthogonality holds by construction.

**T60 and damping.** Per pass through line *i* the required attenuation is
*A*ᵢ(ω) = −60·*m*ᵢ/(*f*s·T60(ω)) dB, with T60(ω) = T_mid·*r*_lo below the low
knee and T_mid·*r*_hi above the high knee; *r* ∈ [0.10, 2.00], low knee
16–1600 Hz, high knee 1000–2100 Hz, T_mid 0.1–20 s — Reference A's ranges
verbatim (`01` A, `02` row 20). The absorbent filter is a broadband gain
*g*ᵢ = 10^{*A*ᵢ,mid/20} cascaded with a first-order low shelf of DC gain
10^{(*A*ᵢ,lo−*A*ᵢ,mid)/20} and a first-order high shelf of Nyquist gain
10^{(*A*ᵢ,hi−*A*ᵢ,mid)/20} at their knees. For the one-pole case
H(z) = *g*(1−*b*)/(1−*b z*⁻¹), DC gain is *g* and Nyquist *g*(1−*b*)/(1+*b*), so
with α = 10^{(*A*(π)−*A*(0))/20} the pole is *b* = (1−α)/(1+α). Every gain is
derived from T60(f) and *m*ᵢ, so the multipliers are accurate rather than fitted —
the whole reason for choosing an FDN.

**Stability.** Orthogonal matrix and |Hᵢ(ω)| < 1 everywhere (`04` §3); clamp
max|Hᵢ| ≤ 1 − 1e−4. At *r*_hi = 2.0 and T_mid = 20 s the longest effective T60 is
40 s and *A*ᵢ is still negative, so the clamp only bites on smoothing overshoot.

**Echo density and onset.** After *p* = *t*/τ̄ passes the network has of order
N^p paths, so Schroeder's **1000 echoes/s** — his figure; the repeated 10 000/s
is Griesinger's via Jot & Chaigne (`05` §8, §11) — is reached at
*p* = log_N(1000·τ̄), ≈1.9 passes or ≈105 ms for Hall at τ̄ = 55 ms. A real room
of matching volume gets there in 44 ms (1000 m³) to 199 ms (20 000 m³) by the t²
law (`05` §8): plausible for a hall, far too slow for a room. Hence the ER feed
is not optional — at *d* = 0.7 the tail is injected with an already-diffused
cluster, so perceived onset density is the ER's, and the input allpasses
(10–35 ms, g ≈ 0.62–0.70) multiply it further. Target mixing time at or under
Polack's √V ms (10 / 32 / 141 ms at 100 / 1000 / 20 000 m³), which Lindau et al.
confirm as the right functional form (R² = 78.6 %) and which is *not* the same
criterion as time-to-1000-echoes, diverging at large V. Schlecht & Habets give
the lever: echo density is a polynomial whose coefficients follow from the delay
lengths, so **mixing time is set by mean delay** (`05` §8) — what τ̄ per type is
for. Plate gets four input allpasses. The Attack contour (§2) shapes the bloom
without touching density.

**Modulation.** Eight incommensurate smoothed-**random** delay modulators, not
LFOs — randomisation suppresses metallic artefacts without the chorus a periodic
sweep gives (`01` B, `02` HW-2, `04` §3); this is most of "Reference B's tail".
Linear fractional interpolation. Pitch bound: detune in cents is
1200·log₂(1+|dτ/dt|), and for a random signal of peak deviation *D* and bandwidth
*B*, |dτ/dt| ≲ 2π·*B*·*D*. Holding ≤ **3 cents** at *B* = 1 Hz gives
*D* ≤ 0.28 ms. Defaults 0.1–0.8 ms and 0.1–1.2 Hz, product constrained to the
3-cent bound at the top of both; Plate uses half depth. If 3 cents still reads as
wobble on a held vocal, the escape hatch is time-varying orthogonal matrix
modulation, which has no pitch modulation at all (`04` §3) — designed for, not
budgeted.

**Decay truncation** is Reference A's 0.04…3.5 (3.5 = linear) as an envelope
multiplier on the FDN output, retriggered by an input envelope follower — one
control, no extra algorithms. **Denormals:** `juce::ScopedNoDenormals` is already
applied host-rate in both processors (`00` §2); add an alternating ±1e−20
injection into one line as belt and braces. **Freeze:** not in v1.

## 5. Parameter changes, bypass, tail reporting

Type switch may be a large jump in sound (`01` D3) but must not click: 30 ms
raised-cosine dip on the wet bus, tables swapped at the minimum, ramp back — no
allocation, no second engine. Size and pre-delay use the §3 crossfade, shared by
ER and late. Decay, damping, EQ, Attack, levels and mix are coefficient changes
with 20 ms one-pole smoothing. Density uses §3's continuous weighting and needs
no crossfade at all.

**Bypass**: there is no per-slot enable flag — modules are present or absent
(`00` §2) — so a removed reverb truncates its tail. The module fades its wet bus
over 150 ms in `reset()`; a real bypass needs a rack change (§8).

**Tail length.** Both processors hardcode `getTailLengthSeconds()` to 0.0
(`SingleModuleProcessor.h:41`, `RackProcessor.h:124`); reverb is the first module
for which that is wrong. Report
**T_tail = preDelay_s + T_mid·max(1, *r*_lo, *r*_hi) + *t*_ER,max + 0.05 s**,
from parameter values rather than DSP state, clamped to a 30 s ceiling so a
20 s × 2.0 setting does not hand the host 40 s. Tails compound along a chain, so
the rack figure is the **sum** over occupied slots, not the maximum.

## 6. CPU and memory

Per instance, stereo, 48 kHz, ops/sample — order of magnitude from the
structures, nothing measured. Input conditioning and EQ ≈ 40 — **estimated when
the EQ was two first-order shelves; it is three biquads now** (§2), so read that
term as low by roughly 20 ops/sample until `measure_reverb` says otherwise, and
note that it is the one term the parametric changed; ER taps ≈ 130 plus
band filters ≈ 25 and diffuser ≈ 60; input allpasses ≈ 16; FDN ≈ 132 (reads,
Householder, absorbent filters, output taps); output stage ≈ 20. **≈ 425
ops/sample**, ~20 M ops/s at 48 kHz, doubling on ER taps during a crossfade and
rising linearly with rate.

Expensive parts are the two `04` predicts: **per-tap filters** (21 one-poles per
channel would add ~130 ops/sample, a third of the budget) and **interpolated
modulated delays**. Fallbacks in spending order: four order-banded filters
instead of 21 (already assumed); the post-ER high-cut alone; modulation on 4 of 8
lines; Density capped at 24 taps at 192 kHz. §4's N = 16 option would add ~120
ops/sample on its own.

**Acceptance budget** — proposed, since none is documented (`00` §3) and the only
comparable figure is BMO Tune RT at 0.934 % median, 48 kHz/128: **≤1.5 % of one
core at 48 kHz/128 and ≤5 % at 192 kHz, per instance**, so eight slots stay under
12 % and 40 %. Measure via `tools/measure/reverb/main.cpp`, do not infer.

**Memory** (float32): ER 0.25 s × 2 ch, pre-delay 0.25 s × 2 ch, FDN Σ ≈ 0.7 s
with modulation headroom, diffuser and allpasses ≈ 0.1 s — ≈1.55 s
mono-equivalent → **≈300 kB at 48 kHz, ≈1.2 MB at 192 kHz** per instance, ≈10 MB
for a full rack at 192 kHz. Allocated in `prepare()` from `sampleRate`.

**Parameters: 30**, inside one slot's 32 host params with no overflow state,
unlike DEQ, leaving **two** spare lanes. The permanent order is `11` §4's
schema table, which is the authoritative copy, and this is the same list in the
same order: Type, Size, Pre-delay, Decay, Source, 2 damping ratios, **the EQ
block — Filter mode, then each of the three nodes as freq / gain / Q, ten in
all** — ER Mode, ER Density, ER Spread, ER High Cut, ER Variation, Mod Depth,
Mod Rate, Width, In High Cut, ER Level, Reverb Level, Mix, Output. Era fields
are constants, not parameters (§1).

*This list said thirty, then twenty-four, and is thirty again — and the two
thirties are not the same thirty.* Both changes happened on 2026-09-21, before
anything shipped, which is what made them free.

**The trim cut six.** **None of the six left the design; each became a
constant** in the per-type block §1 already describes, so everything this
document says about what they *do* stands unchanged — the tail-onset contour in
§2, the decay truncation in §1, the damping knees in §2, the rise exponent *p*
in §3, and ER travelling with the dry signal in §2 are all still here and still
specified. What changed is only that the engine reads them from
`TypeConstants` rather than from a host lane. The six, with the ids `11` §4a
records: `attack`, `decayshape`, `damplofreq`, `damphifreq` (the owner's call,
the frequencies belonging to the onboard EQ), `ershape` and `prelink` (Claude's
call, accepted). `prelink` is a **fixed** constant, `kPreLinkFixed = false`, not
a per-type one, because no type wanted its own answer — so **ER always travels
with the dry signal** and §2's Link switch is no longer offered. Every cut was a
float or a bool, and those re-append safely, since state is plain values keyed
by id; the two choices, Type and ER Mode, were not touched and must not be.

**The Reverb EQ then spent six of the eight lanes that bought, the same day.**
It is now a three-node parametric — low shelf, bell, high shelf, shapes fixed
with no selector anywhere — and the change was **purely additive**: `eqlofreq`
/`eqlo` already *were* node 1's frequency and gain and `eqhifreq`/`eqhi` node
3's, so no id changed meaning and a state file written against the twenty-four
restores every value it still holds. The six new ids are `eqfilter`, `eqloq`,
`eqmidfreq`, `eqmid`, `eqmidq` and `eqhiq`. `eqfilter` became a **four-position
choice** on 2026-09-22 — `Off` / `Lo Cut` / `Hi Cut` / `Bandpass`, on the lane a
bool used to hold, so it cost nothing — and **that count is now permanent at
first ship**, on the same normalisation argument Type and ER Mode carry. Node
3's range was widened to 1 kHz–20 kHz in the same pass. `11` §4c carries all of
it. This document's §2 sentence about where the EQ sits — *pre* both generators
— is what survived; the node count was never load-bearing here.

**The cut/shelf rule is a DSP statement and belongs here.** A node the mode has
made a cut keeps FREQ and Q — a corner is a corner and a resonance is a
resonance — and has **no gain at all**, so its gain never reaches the design.
The gain parameter is *withheld rather than zeroed*, so a trip through a cut
position and back restores the shelf. The middle node is a bell in every
position and FILTER does not touch it.

*This section also said 29 while listing 30.* The odd one is **In High Cut**: §2 introduces it
inside the *Input* sentence beside an explicitly fixed 20 Hz high-pass, §6's CPU
line bundles it into "input conditioning and EQ", and §7 gives it no range row
though every other control has one — so the body read as 29 plus a constant. It
is kept as a parameter (§2 gives it a 2–20 kHz user range, and it is the only way
to darken what feeds both generators independently of the Reverb EQ), marked
**owner confirm** in `11` §4. Dropping it before first ship is free; after that it
is permanent.

## 7. Fixed values to target

| Value | Target | Source | Confidence |
|---|---|---|---|
| ER window | 5–100 ms (200 halls); anchors EBU 15, ISO 50/80, √V | `02` r4; `05` §10.1 | Med — range is folklore |
| Tap count / gain law | 21, 6–48; 1/t spreading × exp. absorption, both | `05` §10.2–10.3 | High |
| Moorer reference | 19 taps, 4.3–79.7 ms | `05` §10.2 | High |
| β; cutoff λ, κ; air absorption | 0.70–0.88; 0.8, 0.2; fit ISO 9613-**2** bands | `05` §11 | **CALIBRATE** |
| Tap spacing | ≥0.9 ms, gaps ≥2 % apart, mutually prime not prime | `05` §11 | High |
| "Boxy" window + ceiling | Δt 5–20 ms (min 2–5); ΔL ≤ −0.6·*t*(ms) − 8 dB | `05` §6.3 | Medium |
| Ripple vs detection | ≤3 dB at −15.3 dB; detected to −21.5 / −27 dB | `05` §6.1–6.2 | High / Med |
| ER vs dry ripple | Σ*P* ≤ −15.8 dB for ≤1 dB RMS | derived | Medium |
| Mono | γ ≥ 0 (Var 0–5); Var 6 complementary comb, sums to unity | `05` §9.3 | High (algebra) |
| Dichotic headroom | ≈10 dB over diotic below 5–10 ms | `05` §6.3 | Medium |
| Lateral fraction / band | 0.10–0.35, mean 0.19, JND 0.05; built 125–1000 Hz | `05` §3 | High |
| LOC / proximity | ≥3 dB below direct in 100 ms; boost <5 ms, cost >7 ms | `05` §4 | Med (contested) |
| ITDG | **not** a design target; halls 15–28 ms | `05` §2, §11 | High (withdrawn) |
| Distance | a = 0.54 ⇒ taper *F*^1.85; DRR direct window 6 ms | `05` §7 | High |
| Velvet / NED | ≥2000 pulses/s (600 if LPF 1.5 k); NED 0.3, 0.7, →1 | `05` §6.4, §8 | High / Med |
| Density ramp Δ | 0.08 | derived | **CALIBRATE** |
| Decorrelation steps | 7 (Variation 0–6) | `01`; `02` r19 | High |
| Pre-delay / Size | 0…+250 ms tail-only; 0.5–80 m | `03`; `02` r7 | High / Med |
| Decay, damping, EQ, faders | 0.1–20 s; 0.10–2.00× at 16–1600 / 1000–2100 Hz; −24…+12 dB; 0…−40 dB | `01` A | High |
| *(note)* the two damping knee spans above are **per-type constants, not knob ranges**, since the 2026-09-21 trim (§6); they are still the spans a type's knee must fall inside | — | `11` §4a | High |
| *(note)* the row above gives **Reference A's** EQ, which was two shelves. The Reverb EQ's own node ranges are `11` §4's table: 16–1600 Hz, 20 Hz–20 kHz and **1 kHz–20 kHz**, opening at 200 Hz / 1 kHz / 6 kHz, each with a Q (outer nodes to `kShelfMaxQ` = 2, the bell to 40). Node 3 carried Reference A's 1000–2100 Hz until 2026-09-22 and it was inherited rather than chosen | — | `11` §4c | High |
| FDN lines / τ̄ | 8; 18–80 ms by type | `04` | **CALIBRATE** |
| Modal density | Σ*m*ᵢ ≥ 0.15·**T60**·*f*s | `05` §8 | High — corrects `04` |
| Echo density / mixing | 1000/s (not 10 000); √V ms, R² 78.6 % | `05` §8, §11 | High |
| Modulation | ≤3 cents ⇒ *D* ≤ 0.28 ms at 1 Hz; 0.1–0.8 ms, 0.1–1.2 Hz | derived; `04` §3 | High / **CAL** |
| Crossfade window | 30 ms raised-cosine | `modules/dim` | High |
| HW-2 Shape/Spread end-stops | — | unconfirmed | **CALIBRATE** |

## 8. Open questions and risks

**Shared-code changes required.** (1) **Tail reporting** — both processors
hardcode 0.0 and neither `ModuleDef` nor `ModuleDsp` has a tail accessor; needs
`tailSecondsForParams(const float*, int)` on `ModuleDsp` mirroring
`latencyForParams` (default 0.0), the single processor returning it, the rack
summing over slots. Touches every module's vtable. **Both halves shipped on
2026-09-21**, and the rack clamps its summed total at `bmo::kMaxTailSeconds` as
well, because eight Lingers in a chain is a legal four-minute tail.
(2) **Mono in → stereo out** — both processors restricted buses to mono *or*
stereo with no conversion (`SingleModuleProcessor.cpp:70-79`); the engine
generates stereo natively from one input, so bus layout was the whole obstacle.
**Shipped in v1, 2026-09-21**, in `core/product/BusLayouts.h`. (3) **Tempo-synced pre-delay**
needs `AudioPlayHead` plumbing that exists nowhere (`00` §2) — out of v1.
(4) **Real bypass** — no per-slot enable flag, so no tail survives one.

**What in Reference B's sound is not public.** No manual exists (tooltips only);
no algorithm, coefficient set or delay table is published for any mode; no
impulse-response, echo-density or latency measurement of either reference was
found anywhere (`01` "Could not verify") — there is nothing to null against.
CALIBRATE by ear: the randomised-modulation distribution, rate and depth that
give the smooth non-metallic tail; the Attack contour's shape; the early/late
balance law in its ER modes; the era grit; and B's decay/size end-stops, which
are tutorial-sourced. The design targets the described behaviour, not the
product.

**Risks.** §4's modal-density finding is the biggest: the line count may have to
rise to 16 and take the CPU budget with it. Image-source tables must be audited
against the comb and flam rules *after* jitter; a failing table is re-seeded, not
patched. The 3-cent modulation bound may still read as wobble on held vocals.
Density at 48 taps and 3 diffuser stages at 192 kHz is the CPU worst case and
should be measured first. Two `05` cautions cut against the design's instincts
and must be tested, not assumed: diffusion does *not* reliably hide reflections,
and Griesinger's 10–100 ms prescription is contested by Pätynen/Lokki. The accent
hue must land in **298.4°–309.2°** (`00` §5), the only window left once FET,
Defang and Dwell are counted.

**Blocking unknown:** none. HW-2's Shape/Spread/Size end-stops stayed unconfirmed
— searches confirmed the *behaviour* (Shape 0 builds explosively and decays
quickly, higher Shape builds slowly and sustains for Spread; Size guidance
~0.15 m to ~38 m) — but they gate only the Energy-mode envelope calibration, not
the architecture.
