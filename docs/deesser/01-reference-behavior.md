# De-Esser Groundwork: Reference Behavior

## 1. Sibilance acoustics

Sibilance is produced by fricative/affricate phonemes — /s/, /z/, /ʃ/ ("sh"), /tʃ/ ("ch"), plus the higher-frequency components of /t/ and /f/ — where turbulent airflow past the teeth/palate creates broadband, noise-like (non-tonal) energy rather than a pitched formant structure. **DOCUMENTED**: alveolar /s/ centers roughly 3–7 kHz (up to 9 kHz for children's voices); post-alveolar /ʃ/ centers roughly 2–6 kHz; spectral centroid reliably separates the two (PMC12499953; Ohio State ICPhS 2007 spectral-measures paper). **DOCUMENTED**: by voice type, male sibilance commonly concentrates 3–6 kHz and female sibilance 6–8 kHz, i.e., roughly an octave higher, tracking shorter vocal-tract/oral-cavity length (Wikipedia "De-essing", collating hardware-manual specs). **FOLKLORE/ANECDOTAL**: sibilant events are commonly described as louder-sounding than adjacent vowels despite similar or lower RMS, because ear sensitivity peaks near 2–5 kHz (Fletcher-Munson region) and the energy is concentrated and un-warmed by harmonic content; typical event duration is short, on the order of 60–200 ms (mixing-guide consensus, not a cited measurement). **DOCUMENTED/FOLKLORE mix**: downstream problems are widely cited — compressors triggering on sibilant peaks and pumping; brightening/high-shelf EQ and saturation/distortion adding harmonics right in the sibilant band; lossy codecs producing pre-echo/artifacts around sharp noise transients; and pre-emphasis schemes used in analog broadcast and vinyl cutting (RIAA/broadcast pre-emphasis boosts highs before transmission/cutting to fight noise, then de-emphasizes on playback) making unchecked sibilance clip or distort the pre-emphasis stage — this pre-emphasis mechanism itself is DOCUMENTED engineering history; its named role as "the reason de-essers exist" is ANECDOTAL/received wisdom repeated in engineering literature.

## 2. Classic hardware designs

**DOCUMENTED**: De-essing dates to 1939 at a film-sound studio; dedicated hardware units followed in the 1970s, and a widely cited 1980s 19" rack de-esser became a studio standard whose "dynamic threshold" scheme compared sibilant-band energy against full-bandwidth level (a ratio, not an absolute threshold) so that gain reduction tracked proportionally regardless of input level — this level-independent detection was novel enough to be popularly (if wrongly) assumed to be that manufacturer's proprietary invention (Wikipedia "De-essing"; gearspace hardware-de-esser retrospective). Why it matters for usability, **FOLKLORE/ANECDOTAL**: a fixed-threshold detector needs re-riding as a vocalist's level changes through a take, while a ratio detector needs one setting for a whole performance.
**DOCUMENTED categories**: wideband (sidechain-filtered) de-essing turns the whole signal down whenever the filtered sidechain crosses threshold; split-band de-essing filters the audio into bands, compresses only the sibilant band, then recombines; dynamic-EQ/bell-cut designs apply a moving narrow-band cut rather than full-range gain reduction, leaving level elsewhere untouched (Wikipedia; AudioTechnology "View From The Bench"). Phase-cancellation designs (subtracting a filtered, level-scaled copy of the sibilant band from the original) are a documented alternative topology used in some hardware/software to avoid the "level pumping" character of gain-reduction detectors — **DOCUMENTED** as a technique, though sourcing on named products is sparse.
**Typical ranges, DOCUMENTED**: detection/target band roughly 2–10 kHz (commonly narrowed to 4–10 kHz in practical settings); attack times from well under 1 ms up to ~10–20 ms; release times roughly 2–60 ms, with ~50 ms attack/50–60 ms release cited as a workable general-purpose pairing (Wikipedia; Mix magazine "Beyond De-Essing"). **FOLKLORE/ANECDOTAL**: knee is usually soft in musical implementations to avoid audible snapping; lookahead of a few milliseconds is used in software designs to catch the fast onset of a sibilant burst before it overshoots, trading a small fixed latency for cleaner attack.

## 3. Software de-esser feature set (generic)

**DOCUMENTED/observed-consensus** feature list across well-regarded software de-essers: a threshold-based mode alongside a relative/ratio ("level-independent") detection mode; adjustable center frequency and bandwidth/Q for the sibilant band; a range/depth or maximum-reduction limit; a switch between wideband and split-band (or multiband) operation; a listen/audition solo of the detection band; optional lookahead; stereo-link or mid-side linking so stereo images don't wander during reduction; and preset modes tuned for a single vocalist versus mixed/ensemble material (sonible blog; iZotope Nectar documentation; general plugin-manual conventions).

## 4. Known artefacts and chain placement

**DOCUMENTED/consensus**: over-reduction causes lisping (audible "s"-to-"th"-like softening) and dulling/muffling of the vocal; wideband detectors can pump adjacent high-frequency ambience/room tone; split-band recombination can introduce phase/crossover coloration at the band edges; linear-phase split filters can pre-ring on transient sibilant onsets; lookahead adds fixed latency (Wikipedia; peak-studios pre-ringing article; general plugin literature). **FOLKLORE/ANECDOTAL** (interview/tutorial-sourced, not measured): typical reduction is 2–6 dB, most sources converging on 2–4 dB for a transparent result and reserving deeper cuts for spot problems; placement is genuinely contested — some engineers de-ess before compression/EQ to avoid re-exciting sibilance that compression would otherwise amplify, others de-ess after compression and EQ (compression raises the noise floor, making sibilance easier to detect and control), and de-essing is commonly recommended before reverb/delay sends so sibilant energy is not smeared through the effect.

## 5. Newer detection research

**DOCUMENTED (patent/technical literature, verify claims independently)**: proposed features include sibilance spectral flux (rate of change of the power spectrum within the sibilant band, faster than non-sibilant speech) and sibilance spectral flatness (geometric/arithmetic mean ratio distinguishing noise-like fricative energy from tonal voiced content), described in sibilance-detection patent filings (USPTO 10867620, 12462826). Spectral flatness and zero-crossing rate are established general voiced/unvoiced discriminators in speech-processing literature (math.uci.edu voiced/unvoiced classification paper), applicable to sibilance gating. **UNVERIFIED/FLAG**: machine-learning phoneme-aware sibilance detection is marketed by several plugin vendors (sonible AI-de-esser blog) but no peer-reviewed AES/DAFx/ICASSP paper describing a specific published ML sibilance-detection architecture was found in this search; treat vendor claims as unverified until a citable paper is located.

## 6. Target figures for testing

| Parameter | Target value | Confidence |
|---|---|---|
| Detection/target frequency range | 2–10 kHz, adjustable center | High (documented) |
| Practical working band | 4–10 kHz | High (documented) |
| Male sibilance concentration | ~3–6 kHz | Medium (documented, generalization) |
| Female sibilance concentration | ~6–8 kHz | Medium (documented, generalization) |
| Detection band shape | Bandpass/shelf sidechain (wideband) or narrow bell (split/dynamic-EQ) | High (documented) |
| Attack time | <1 ms to ~20 ms | Medium (ranges vary by source) |
| Release time | 2–60 ms (50–60 ms common default) | Medium |
| Lookahead | 0–10 ms typical | Low-Medium (anecdotal) |
| Max reduction (range/depth ceiling) | 10–15 dB hard limit typical | Low (anecdotal, plugin-convention) |
| Typical musical reduction | 2–6 dB (2–4 dB most common) | Medium (anecdotal, wide consensus) |
| Acceptable added latency | ≤10 ms for real-time tracking use | Low-Medium (anecdotal) |

Blocking unknown: none — figures above are directly usable as test targets, with confidence levels flagging where only anecdotal/interview consensus exists.

## Sources

- [Beyond spectral moments: sibilant fricatives, children's speech (PMC)](https://pmc.ncbi.nlm.nih.gov/articles/PMC12499953/)
- [Spectral measures for sibilant fricatives (Ohio State, ICPhS 2007)](https://www.ling.ohio-state.edu/pdlg/LiICPhS2007.pdf)
- [De-essing — Wikipedia](https://en.wikipedia.org/wiki/De-essing)
- [Beyond De-Essing — Mix magazine](https://www.mixonline.com/recording/beyond-de-essing-375623)
- [View From The Bench: DE-esser — AudioTechnology](https://www.audiotechnology.com/tutorials/view-from-the-bench-de-esser)
- [Moving Beyond Traditional De-essers — sonible](https://www.sonible.com/blog/beyond-traditional-deessers/)
- [How Artificial Intelligence has Improved the De-esser — sonible](https://www.sonible.com/blog/ai-improved-deesser/)
- [Pre-ringing in music production — peak-studios](https://www.peak-studios.de/en/pre-ringing-musikproduktion/)
- [Sibilance detection and mitigation — US Patent 10867620](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/10867620)
- [Adapting sibilance detection — US Patent 12462826](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/12462826)
- [Audio de-esser independent of absolute signal level — US Patent 11322170](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/11322170)
- [Voiced-unvoiced-silence classification of speech (UC Irvine)](https://www.math.uci.edu/~yqi/ieee00222883.pdf)
- [De-Esser Module — iZotope Nectar documentation](https://help.izotope.com/nectar2/content/nectar%202%20help%20documentation/de%20esser%20module.htm)
