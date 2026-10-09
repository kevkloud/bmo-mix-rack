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

**ER Mode** remains Taps / Energy (Blend was cut after the M2 listening pass, 2026-09-26) for the envelope itself: Energy mode
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

### As built in M3a (2026-10-02, on ICE QUEEN)

`modules/reverb/dsp/LateNetwork.h` departs from this section in the places below.
Each was forced by a measurement against `11` §6, and each is argued in full
where it is coded. Everything else above stands.

- **Hadamard, not Householder.** Householder is maximally mixing at four
  lines, not eight. At eight its diagonal is 0.75, so each line mostly feeds
  itself, and the late envelope recurred at each type's shortest line
  (autocorrelation up to 0.28, against 0.2). Hadamard, done as a fast
  Walsh–Hadamard transform, brought it to 0.08–0.16 at about the same cost.
  **The line count must now be a power of two: 16 stays open, 12 does not.**
- **Second-order shelves, half an octave outside each knee.** First-order
  shelves cannot meet "mid within 5 % whatever the multipliers" with knees
  three octaves apart: the mid band read 25 % off at a 0.25 multiplier. RBJ
  shelves at S = 1 (monotonic, so the clamp above still bounds them), placed
  so each plateau starts at its knee, hold the mid within 1.5 %.
- **The input diffusers may not ring longer than half of DECAY.** At
  g = 0.66 and 18 ms an allpass rings 0.3 s on its own, so DECAY 0.3 s
  measured 0.36 s. Each diffuser's gain is now capped by DECAY. Above about
  1.1 s the cap never bites.
- **Denormals are flushed, not injected.** A ±1e−20 injection means a reset
  network is never silent, which breaks `11` §6's "zeros in, exactly zeros
  out". Everything the network stores is zeroed below 1e−15 instead.
- **SIZE scales τ̄ in proportion from each type's own SIZE, floored at
  5 ms, and the tail's level by √(τ̄ / the type's τ̄).** The first is a
  reading of `11` §1 ("the late network scales with the taps under SIZE").
  The second exists because a network's energy at a given T60 grows as
  T60/τ̄: Hall at 1 m peaked +0.8 dBFS on pink noise at −18 dBFS RMS. Both
  are CALIBRATE.
- **A length change runs two whole paths, weighted by when a sample was
  written** (third form, 2026-10-03). The old read goes through the old
  filters at the old level and the new read through the new filters at the
  new level. A sample written before the move is read at the old delay, in
  full, and never again; a sample written after it is read at the new delay;
  the two weights cross over across the 30 ms after the move starts; and at
  no instant do the two paths together weigh more than one. So the reads of
  a line carry no more energy than was written to it, moving or not.

  *Why it is this and not §5's plain 30 ms crossfade:* a crossfade in read
  time re-reads the line at its new delay, and reading at a longer delay
  replays samples that have already been round the loop. Every move put
  energy back, and SIZE toggling 12 ↔ 30 m every 64 blocks at DECAY 20 s
  reached +573 dBFS in a minute. The two earlier forms had their own
  failures: a redesign landing in one step at the end of the fade was the
  largest step in the move, and blending the *coefficients* does not keep a
  filter's gain-times-shelf product (a 27 dB burst on a full-range move).

  *What it costs, measured on noise held through one move, Room, DECAY
  1.8 s, 48 kHz, in 10 ms windows:*

  | move | deepest window | within 1 dB of settled | the move lasts |
  |---|---|---|---|
  | 12 → 30 m | 18 dB down | 70 ms | 111 ms |
  | 30 → 12 m | 1.8 dB down | 160 ms | 111 ms |
  | 12 → 80 m | silent | 340 ms | 246 ms |
  | 80 → 12 m | 0.9 dB down | 300 ms | 246 ms |

  A line that grows is quiet between its old delay and its new one, because
  nothing written since the move has reached the new delay yet. A move lasts
  the longest line, old or new, plus 30 ms, and the next move waits for it,
  so SIZE under automation steps at that pace. **The ER generator keeps §3's
  30 ms crossfade**: it is feed-forward, has no loop to feed, and so cannot
  grow. "ER and late sharing the scheme" (§5) no longer holds, on purpose.

  **On held noise the cost passes; on a decaying tail it stays.** With
  signal still arriving, the network refills and settles at the new SIZE's
  own level, so a move costs only the dip in the table above. With nothing
  arriving, whatever a move drops is gone: every move makes the tail
  quieter than SIZE held at either end, for good, and **shrinking costs
  more than growing**. A shrinking line reads its pre-move samples at the
  old delay to the end, and the new path stays silent until they are done,
  so what was written in between is lost. Measured on ICE QUEEN (QA's
  probe, `gap`; Room unless named, DECAY 5 s, a 10 ms burst at −18 dBFS RMS
  at 0, one move at 0.3 s, 48 kHz / 32, the late output alone), the level
  1–2 s after the move against SIZE held at the old size:

  | move | during the move | 1–2 s after, for good |
  |---|---|---|
  | 12 → 13 m | 3.2 dB down | −1.2 dB |
  | 12 → 30 m | 17.4 dB down | −1.2 dB |
  | 12 → 80 m | silent, 80 ms more than 20 dB down | −1.2 dB |
  | 0.5 → 80 m | silent, 110 ms more than 20 dB down | −5.0 dB |
  | 30 → 12 m | 6.6 dB down | −4.5 dB |
  | 80 → 12 m | 11.8 dB down | −10.8 dB |
  | 80 → 0.5 m | 20.9 dB down | −19.5 dB |
  | Ambience 80 → 0.5 m | 20.4 dB down | −18.3 dB |

  Against SIZE held at the *new* size the figures are within 0.2 dB of these.

  **Under automation the losses add up**, one per move. QA's `autolevel`
  (Room, DECAY 20 s, both multipliers 2.0, 48 kHz, SIZE written once per
  32-sample block; ICE QUEEN): the loop energy's T60, fitted 5–35 dB under
  its peak after a 10 ms burst, and the output level on held noise at
  −18 dBFS RMS over 10–60 s against SIZE held at 12 m:

  | SIZE | tail T60 | held noise |
  |---|---|---|
  | held at 12 m | 39.45 s | 0 dB |
  | LFO 12..13 m, 10 s period | 21.58 s | −3.22 dB |
  | LFO 12..15 m, 10 s period | 18.44 s | −6.05 dB |
  | LFO 12..30 m, 4 s period | 6.65 s | −8.79 dB |
  | toggled 12 ↔ 30 m every 64 blocks | 1.91 s | −12.68 dB |

  `reverb_dsp_tests` pins the held row, the first LFO and the toggle (on the
  output's energy, which gives 39.45, 21.6 and 1.91 s and −3.22 dB), so a
  change to the loss in either direction fails.

  **Frosty, 2026-10-03: "a held SIZE is untouched; automating SIZE thins the
  tail" is the behaviour for 0.2.6**, with gliding the line lengths as the
  fallback if the listening pass disagrees. SIZE is a set-and-leave control.

  **Frosty accepted this trade-off on 2026-10-03, with a fallback named.**
  If the gap on a growing SIZE move turns out to matter in use, the
  fallback is to **glide the line lengths** instead: no gap and no replay,
  at the price §5 refused it for, a pitch bend across the whole tail for as
  long as the move lasts ("a chorus and not a room"). It is not built. A
  third option was set aside: letting the old room ring out beside the new
  one, which has no gap and cannot grow but doubles the tail's memory and
  its cost during a move.
- **DECAY and both multipliers wait for a length move to end**, departing
  from §5's 20 ms smoothing while SIZE moves. A move takes no new request
  until it is over, so under SIZE automation the three reach the network once
  a move: every 111 ms (Room 12 ↔ 30 m) to 289 ms (Ambience 0.5 → 80 m) at
  48 kHz, against one 32-sample block (0.7 ms) with SIZE held. Left on
  purpose: the two-path sum during a move is held by measurement, and that
  measurement ran with the coefficients still. `reverb_dsp_tests` pins it.

Four more, from QA's two passes on PR #38 (2026-10-03):

- **The absorbent filters run in double.** In float, at 96 and 192 kHz, the
  rounded coefficients realised a DC loop gain of up to 1.0071 and the tail
  grew without limit. The lines stay float.
- **`reset()` and `prepare()` build from the current settings**, never from a
  move in flight; an unprepared network holds no lengths and outputs zeros;
  and the search for line lengths is bounded.
- **The early reflections' one-poles flush below 1e−15 too**, so a silent
  instance is exactly silent with flush-to-zero off.
- **The level at the far corner is intended** (Frosty, 2026-10-03). Plate,
  DECAY 20 s, both multipliers at 2.0, REVERB 0 dB, on noise at −18 dBFS RMS,
  peaks at +3.4 to +4.1 dBFS. A 40 s tail holds that energy, nothing scales
  the level by decay, and REVERB is the control for it.

Two items are red and recorded, not hidden, both Plate: modal density, Σ*m*ᵢ
= 0.146 s against 0.15 s (this section predicted it), and late-envelope
autocorrelation of 0.202 against 0.2. M3b's modulation or M4's line count
takes them back.

### As built in M3b (2026-10-05 and 2026-10-06, on ICE QUEEN)

**Modulation** (`LateNetwork.h`, 2026-10-05). Each line's delay wanders
toward a new random target every half period along a smoothstep, each line
at its own fraction of MOD RATE; it is random and not an LFO. The 3-cent
bound holds by construction, so depth and rate trade at the top: 0.283 ms at
1 Hz, the full 0.8 ms only under 0.35 Hz. Reads are Lagrange, six points
under 88.2 kHz and four above; four points at 48 kHz took enough off the top
each pass to put HIGH × 2.0 at 1.69. **Plate's late-envelope red is
cleared**: 0.202 → 0.177 against 0.2 (0.189 since the bound moved, below),
and asserted since 2026-10-07; until then the test only printed it. Plate's
modal density is still red, and is M4's.

Three things QA's review of PR #55 added to this, 2026-10-07:

- **The bound is built at 2.94 cents, and holds while the knobs move.** It
  was built at 3.0001 and tested with MOD DEPTH and MOD RATE held: one MOD
  RATE move 0.1 → 1.2 Hz at depth 0.8 read 3.0006. A moved knob lands a
  little over what is built (0.4 % at 192 kHz, not run down), so the build
  is 2 % under and the test moves both knobs across their ranges: 2.48
  cents at fixed corners, 2.94 with a knob moved.
- **Modulation shortens the top of the tail, and that is the price of
  it.** An interpolated read loses a little off the very top on every
  pass. Room, DECAY 2 s, 48 kHz, T60 at the default modulation over T60
  unmodulated: 0.95 at 8 kHz and 0.82 at 12 kHz at 12 m; 0.69 at 12 kHz at
  0.5 m; with HIGH × 2.0 at 0.5 m, 0.81 at 8 kHz and 0.63 at 12 kHz, so
  HIGH × 2.0 is worth about × 1.27 at 12 kHz in the smallest room. (QA's
  one-octave bands read 0.87, 0.79 and 0.77 at 12 kHz for the same rows.)
  At the corners of depth and rate a DECAY 20 s × 2.0 tail falls 1.72 to
  2.05 dB a second where 1.5 is designed. **Pinned, not compensated**: a
  static gain that put the loss back would take the loop over unity
  whenever a read landed on a sample, and §4's rule is that the loop always
  loses. An allpass read would keep the top and is a different sound.
  Frosty heard and passed the tail as it is on 2026-10-06; whether the top
  wants holding up is a voicing question for M4.
- **The loop's loss is tested second by second**, on energy, not on the
  peaks of 5 s windows.

**The input stage** (`InputStage.h`, 2026-10-06) is §2's first paragraph,
built: a 20 Hz high-pass, DARKEN, then the three Reverb EQ nodes, on the
one signal both generators are fed (the mid of the input), never on the
dry path. Where it departs from or adds to this document:

- **Both fixed-order filters are one pole.** §2 gives neither an order. The
  high-pass is within 0.013 dB of −3.01 at 20 Hz from 44.1 to 192 kHz.
- **DARKEN's corner is exact; its slope below the corner is not the analogue
  one near the top of its range.** The coefficient is solved for −3.01 dB at
  the knob's frequency at every rate. Away from the corner a digital pole
  is not the analogue curve the EQ page draws, and the gap grows as the
  rate falls. At 48 kHz and the default, 20 kHz, it is **1.45 dB down at
  10 kHz where the page draws 0.97** (0.46 against 0.26 at 5 kHz; worst
  0.53 dB, at 12.4 kHz). With the corner low the gap is far down the skirt
  instead: 2.6 dB at 20 kHz with the corner at 2 kHz, where the page draws
  −20 dB. Worst gap anywhere up to 20 kHz, over corners 2 to 20 kHz: 3.1 dB
  at 44.1 kHz, 2.6 at 48, 0.62 at 96, 0.15 at 192. A pole-zero fit was
  worked through for the default and is no closer (0.52 dB at 10 kHz, the
  error the other way). **The EQ page draws the engine's own law since
  2026-10-06** (`InputStage::lowPassDbAt` at the rate the page is drawn
  at), on Frosty's word: "option 1 if it doesn't increase cpu
  significantly". It is paint and costs the audio thread nothing.
- **DARKEN is not transparent at 20 kHz** and is not bypassed there: a
  bypass at the end stop would be a step in the response one detent wide.
- **A coefficient move is a straight line over 20 ms that lands exactly**,
  not §5's one-pole smoothing: each node's five state-variable coefficients
  and DARKEN's one. A line between two stable filters is stable throughout,
  and it ends, which a one-pole approach never does. Measured on 97 Hz at
  −18 dBFS, node 1 flat → +12 dB with DARKEN 20 → 2 kHz: the largest sample
  step during the move is 0.0054 against 0.0057 for the settled +12 dB
  signal (the house limit is 1.5×).
- **A flat node returns its input bit for bit**, and its filter runs anyway
  so its state is the signal's when a gain move starts. At the defaults the
  EQ adds nothing; the high-pass and DARKEN are always in.
- **The stage runs in double and flushes its states below 1e−30** every 64
  samples. The slowest setting the schema allows (a 20 Hz bell at Q 40,
  +12 dB) reaches exactly zero 78 s after an impulse and never passes
  through a subnormal; its ring is under −60 dB re the impulse after 12 ms,
  so the tail report does not count it.
- **The reported tail does not count the stage's own ringing** (QA,
  2026-10-07). DECAY 0.1 s with the bell at 20 Hz, Q 40, +12 dB reports
  0.231 s, and the wet signal stays above −60 dB re its peak until 1.33 s
  (0.67 s with the bell at 100 Hz). That is the far corner of the EQ; at
  any setting a room would use the tail outlasts the filter. Not charged to
  the report; it would be a term in `tailSecondsFor` if Frosty wants it.
- **A frequency jump on a sharp node steps more than the house rule
  allows** (QA, 2026-10-07). The bell at Q 40, +12 dB, jumped 20 Hz →
  20 kHz in one request under a 97 Hz tone: the largest sample step is
  2.38× the signal's own, against the rule's 1.5×, with the peak only
  0.22 dB over. Gain and DARKEN moves are within 1.031×. The 20 ms move is
  a straight line in the filter's coefficients, which is not a straight
  line in frequency. Gliding the frequency itself and redesigning along the
  way, as BMO DEQ does, would close it. **Open, and with Frosty.**
- **One NaN in latches the stage until `reset()`**, as it latches both
  generators on `main`. The rack's guard upstream is what protects it; not
  a regression and not changed.
- **`setBypassedForMeasurement` is a test hook in a shipping header**, and
  three tests run with MOD DEPTH at 0, under the knob's 0.1 minimum. Both
  are deliberate and both are recorded here because a reviewer asked: no
  parameter reaches either.
- **Four early-reflection tests measure the generators with the stage
  out** (`renderBare` in `reverb_dsp_tests`): tap gain as a DC sum, energy
  after the span, the 5 ms energy windows and the DENSITY sweep's fixed
  window. A high-pass has no DC to sum and follows every tap with an 8 ms
  tail. Every other row plays through the stage.
- **The analyser tap is on the stage's output** (Frosty, 2026-10-06: "it
  should show the output, with EQ applied"). The spectrum behind the EQ
  curve is what the room is given, one channel, and moves with the knobs.
  It was on the input until then. It is not the module's output.

**Heard by Frosty on 2026-10-06**, on monitors and headphones, the amp in
stereo, from the two sets rendered on ICE QUEEN
(`testing-notes/linger-listening-set-2026-10-06-m3b.md` has every answer):

- Modulation: "flutter is gone"; on the held note "ring is gone, depending
  on type it reads as wobble, but in a good way"; the other types "sounds
  good"; SOURCE "sounds better".
- The input stage: DARKEN "works as expected, but maybe range down to
  1khz"; the cuts, shelves, bell and bandpass all pass; and of the flat
  default against M3a, with the high-pass and DARKEN now always in, "i like
  this one more".

**Decided by Frosty on 2026-10-06:**

- **Decay truncation is left out**: "leave it out. I'm happy where we're
  at". `decayShape` stays in `TypeConstants` at 3.5, linear, for every type
  and the engine does not read it. §1's contour is not built.
- **The 150 ms wet fade in `reset()` is not buildable in the DSP** and is
  recorded as such: nothing plays after a reset for a fade to act on, and a
  module cannot know one is coming. §5 and `11` §6's Bypass row asked for
  it. Frosty: "record as not buildable and recommend making a bypass". The
  recommendation is §8 (4): a per-slot bypass in the rack, which lets a
  module's tail ring out or fade while the slot is still there.
- **DARKEN's range is 1 to 20 kHz since 2026-10-07; it was 2 to 20.**
  Frosty, on hearing 2 kHz: "maybe range down to 1khz", and the next day
  "1k is the call". It is a range change on a schema that froze at 0.2.6,
  made once and on purpose while Linger has been installed on ICE QUEEN and
  nowhere else. State restores unchanged; an automation lane written against
  the old range reads lower. §2 and §6 still say 2–20 kHz where they tell
  the parameter's history.

**ATTACK, the onset bloom** (`LateNetwork.h`, 2026-10-07). §2 asks for "a
rising envelope on the FDN *input*". As built, **each line is fed the
diffused input at its own delay and its own level**: the shortest line at
once and quietly, the last ATTACK × 120 ms later and loudest, the levels
normalised so their mean square is one. Where it departs from §2, and why:

- **Delays, not an envelope.** An envelope needs something to start it and
  continuous audio has no onsets; delays are linear, need no trigger and
  treat every sample alike. Frosty approved rising taps on 2026-10-06.
- **One tap a line, not eight taps into one input.** Eight rising taps
  summed ahead of the network were built first and are a sparse FIR in front
  of everything: the late tail's spectral flatness fell from 0.775 to 0.528
  on Room, 0.864 to 0.628 on Chamber, 0.924 to 0.718 on Hall, 0.931 to 0.770
  on Cavern and 0.529 to 0.382 on Ambience (the floor is 0.3). Fed a line
  each: 0.768, 0.873, 0.925, 0.927 and 0.522. Plate, at ATTACK 0, is
  untouched in both, 0.759.
- **Measured** on Room at 12 m, DECAY 1.8 s, the tail fed directly: half of
  the tail's first 400 ms is in by 87.0 ms at ATTACK 0, 97.0 at 10 %, 115.3
  at 30 %, 150.1 at 65 % and 186.0 at 100 %, so the full span holds the bulk
  of the tail back by 99 ms. The whole tail's energy stays within 0.08 dB
  from 0 to 100 %. The tail's first sample does not move.
- **The reported tail gains ATTACK × 0.12 s** (`DspCore::tailSecondsFor`),
  since the bulk of the tail ends that much later: Room 36 ms, Chamber 42,
  Hall 60, Cavern 78, Plate 0, Ambience 12.
- **A change of ATTACK crossfades two feeds over 30 ms.** It has no host
  lane, so in a host this happens only with a TYPE change. On a held 440 Hz
  tail, 30 → 100 %: the largest sample step during the move is 0.00357
  against 0.00356 settled.
- **A bloom inside a fitted range reads as a longer decay in a short
  band.** With Room's 36 ms the 50 Hz band at LOW × 0.25 fits 0.631 s where
  the filters alone give 0.557. The damping test runs with ATTACK off for
  that reason; the T60 and tail-report tests run with it on.
- **CPU**, `measure_reverb bench worst`, on a busy ICE QUEEN (2026-10-07):
  48 kHz / 128 1.357 → 1.370 %. At 192 kHz / 32 the machine was moving the
  figure by more than a point between runs; the best single runs with the
  bloom in were 5.23 %, against 5.13 % measured without it the day before.
  That is past the 5.18 % Frosty accepted and is with him.
- **The energy is the same and the peak is not.** The lines fed last are
  fed up to 4.9 dB harder than without a bloom, so a transient's tail has a
  peakier first loud arrival: a snare through Hall, tail only, peaks at
  −30.4 dBFS at Hall's 50 % against −33.7 with the bloom off.
- The span, the first line's level (0.12), the curve (position^1.5) and the
  order the lines are fed in are **CALIBRATE**. Not heard yet.

**`11` §6's last three rows** (2026-10-09, on ICE QUEEN), which close M3b's
build. Two of them measure a target this document set and the engine does
not meet; both are written down as they measured.

- **Mixing time is over Polack's √V ms for every type, by a factor of 1.3
  to 6.** Normalised echo density (Abel and Huang, 20 ms window) reaching
  0.9, from the impulse, each type at its own SIZE, SOURCE and ATTACK:

  | type | mixes at | at SOURCE 0 | √V for its SIZE |
  |---|---|---|---|
  | Room, 12 m | 154 ms | 168 ms | 26 ms |
  | Chamber, 18 m | 185 ms | 192 ms | 48 ms |
  | Hall, 34 m | 258 ms | 260 ms | 123 ms |
  | Cavern, 55 m | 323 ms | 328 ms | 254 ms |
  | Plate, 22 m | 87 ms | 87 ms | 64 ms |
  | Ambience, 8 m | 83 ms | 129 ms | 14 ms |

  The paragraph above this record says why: mixing time is set by the mean
  delay, and eight lines behind two diffusers (four on Plate) take several
  passes to fill in. ATTACK adds to it by design, 3 ms on Ambience to 59 on
  Cavern. **Printed and pinned, not met**: no type may mix later than this
  by more than a tenth, SOURCE at its default may never mix later than
  SOURCE 0 and must be 10 ms sooner on Room and Ambience, and the gap to
  √V is M4's, with the mean delays. Plate is the one type SOURCE does not
  help.
- **Denormals: nothing runs slow.** Flush-to-zero and denormals-are-zero
  off, a 0.5 s burst, a minute of silence: the slowest 5 s of the silence
  took 55 ms against 52 for the 5 s holding the burst at the defaults, and
  68 against 67 with every filter in the module ringing. The output is
  exactly zero by the end and never a subnormal.
- **A held note through the tail wanders by more than 3 cents, and §4's
  3 cents was never a claim about that.** Every line is held under 3 cents.
  What comes out is eight lines summed and heard again on every pass, and
  the phase of a sum of paths wanders further than any one path, most
  where they nearly cancel. A 1 kHz sine through the tail alone, read in
  50 ms windows over 40 s: **1.68 cents RMS at the default modulation with
  peaks to 11.9; 2.93 to 3.00 cents RMS at the corners of depth and rate
  with peaks to 17.3.** `11` §6's Modulation row asks for a peak within 3
  cents on exactly this measurement; it is not met and cannot be by
  bounding the lines. The deviation's spectrum has no line: the largest
  holds 3.8 % of the power, so it is randomised and not a chorus, which is
  the other half of that row and does hold. Frosty heard the held note on
  2026-10-06: "ring is gone, depending on type it reads as wobble, but in a
  good way". The test bounds the RMS at what it measured and the spectrum
  at a tenth.

**M3b's build is complete with these.** What is left before M4 is Frosty's
ear on ATTACK and the decisions listed in
`testing-notes/linger-m3b-qa-pr55-2026-10-07.md`.

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
from parameter values rather than DSP state, clamped to a 40 s ceiling (30 s until 2026-10-02) so a
setting at the corner (40.8 s of arithmetic) hands the host 40 s and no more. Tails compound along a chain, so
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

*As measured with the tail in (M3a, 2026-10-03, on an idle ICE QUEEN):* the
worst case at 192 kHz / 32, SIZE 80, DENSITY 100, is **4.77–5.18 %** across
the types, a hair past the 5 % line for Room and Plate. **Frosty accepts it
at 192 kHz** — "a high-fidelity rate where added cost should be expected" —
so the measured **5.18 %** stands as accepted. It is not a new budget:
whatever M3b's EQ and modulation add on top is measured and brought to
Frosty, not assumed to fit. 48 kHz / 128 stays at ≤1.5 % (measured
1.10–1.22 %).

*As measured in M3b (on ICE QUEEN, `measure_reverb bench <rate> <block>
worst`, median of five):* modulation took 48 kHz / 128 from 1.10 to 1.30 %
and 192 kHz / 32 from 4.58 to 5.19–5.21 % (2026-10-05). The input stage,
measured before and after in one sitting on 2026-10-06, adds 0.04 points at
48 kHz / 128 (1.285 → 1.325 %) and 0.14 at 192 kHz / 32 (4.990 → 5.126 %).
The stage's filters run whether or not the EQ is flat, so those are its
whole cost. Run to run the machine moves these figures by about 0.2 points
at 192 kHz, which is more than the stage costs.

***The whole of PR #55 against `main`, which is the figure that matters and
which the lines above never gave*** (QA, 2026-10-07, ICE QUEEN, same bench,
medians of five, three interleaved passes, `main` 5e9cf91 against
`5fa2db5`: modulation, the input stage and DARKEN's range, without ATTACK):

| | `main` | PR #55 | |
|---|---|---|---|
| 48 kHz / 128 | 1.122–1.124 % | 1.361–1.471 % | budget 1.5 % |
| 192 kHz / 32 | 5.583–5.823 % | 6.639–6.880 % | about +1.0 point, +18 % |

The machine read `main` itself a point higher that day than the 4.58 % it
gave on 2026-10-05, so the 192 kHz column is to be read as a difference:
**M3b costs about one point of a core at 192 kHz / 32, nearly all of it the
modulated reads.** Frosty accepted 5.18 % at 192 kHz on 2026-10-03 for M3a
and said it was not a budget; this is past it and **is with him for a
decision**. At 48 kHz the PR is inside the 1.5 % budget with less margin
than the earlier lines suggested. ATTACK adds 0.013 points at 48 kHz and
about a tenth at 192.

**These are figures for held settings.** While a length move is in flight
the tail runs two paths and the early reflections rebuild their table, and
at 192 kHz / 32 QA measured a mean of about 10.5 % of the block with a 99th
percentile near 40 % (Room, SIZE 30 ↔ 12 m toggled every block and every 64
blocks; 2026-10-03, ICE QUEEN, and the same before and after the move was
reworked that day: 10.4 % and 39.9 % on a quiet machine). No dropout at
that; it is a cost of moving SIZE or TYPE, not of holding them.

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
| HW-2 envelope contour and duration end-stops | — | unconfirmed | **CALIBRATE** |

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

**Blocking unknown:** none. HW-2's envelope and size end-stops stayed unconfirmed
— searches confirmed the *behaviour* (Shape 0 builds explosively and decays
quickly, higher Shape builds slowly and sustains for Spread; Size guidance
~0.15 m to ~38 m) — but they gate only the Energy-mode envelope calibration, not
the architecture.
