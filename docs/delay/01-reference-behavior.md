# Delay Family Reference Behaviour

Research conducted on AURORA, 2026-09-20. Web sources only; no hardware measured directly. Prose uses generic family names, and a source is cited by its kind (a standard, a paper, a maker's manual or datasheet) with no product name and no link: the repository is public, and the names and links the first version carried were taken out on 2026-10-01.

## Tape echo

- **Wow/flutter**: Wow = FM of the signal ~0.5–6 Hz; flutter = FM ~10–100 Hz [DOCUMENTED, AES6-2008 standard] (AES6-2008; an encyclopedia article on wow and flutter measurement). Depth is expressed as peak speed deviation in percent (e.g. 0.1% ≈ 1.7 cents pitch wobble) [DOCUMENTED] (an encyclopedia article on wow). In tape echo units specifically, rate/depth and EQ are stated to scale with delay time, and "age/wear" controls scale wow/flutter depth [DOCUMENTED product description] (a tape-echo pedal's user manual).
- **Head bump**: playback-head low-frequency rise from long-wavelength response; roughly 50–60 Hz at 15 ips tape speed, 100–120 Hz at 30 ips, with a small dip just above the bump [DOCUMENTED, engineering explanation] (a recording-engineering forum thread; an article on low-frequency response in tape).
- **HF loss per repeat / saturation**: each pass through record-play adds progressive HF rolloff and soft magnetic saturation/compression; documented qualitatively in tape-modeling literature (hysteresis-based neural/physical models) [DOCUMENTED mechanism, informal on exact per-repeat dB] ("Neural modeling of magnetic tape recorders", arXiv 2305.16862).
- **Self-oscillation**: feedback/regeneration above ~100% recirculates without decay and builds into howl/noise; widely stated as the defining "runaway" behaviour and a classic dub technique [DOCUMENTED product/engineering consensus, FOLKLORE on precise threshold since it is control-position dependent per unit] (two pedal makers' product and technique pages).
- **Speed-change pitch glide**: changing capstan/motor speed produces a continuous pitch glide together with a delay-time change — standard tape-echo "speed knob" behaviour [FOLKLORE/consensus description, not a cited measurement].

## Bucket-brigade (BBD) analog delay

- **Companding**: compressor before the BBD, expander after, to fight the BBD's limited dynamic range/noise floor [DOCUMENTED] (two engineering explainers, on bucket-brigade devices and on companders).
- **Clock noise & filtering**: anti-aliasing filter before the BBD and a steeper reconstruction filter after it (often two Sallen-Key stages), cutoffs set below the Nyquist of the lowest clock rate used, to suppress clock feedthrough/hiss — this is the mechanism of BBD "darkening" at longer delay times [DOCUMENTED, patent/engineering literature] (an engineering explainer on bucket-brigade devices).
- **Delay time limits**: a widely-used 4096-stage BBD chip datasheet specifies 20.48–204.8 ms delay range, e.g. ~205 ms at a 10 kHz clock, ~75 dB S/N [MEASURED, manufacturer datasheet] (the chip's datasheet; an engineering explainer on bucket-brigade devices). Pedals cascade multiple chips for up to ~1.5 s [DOCUMENTED, product spec].

## Early digital delay (rack units)

- Typical late-1970s/1980s rack units used 12-bit (or 12+1 "13-bit") converters, sample rates deliberately kept low (~26–32 kHz) to conserve RAM for delay time, giving stated bandwidths on the order of 10 Hz–16 kHz, ~80 dB S/N, THD <0.05% on flagship models [MEASURED, product spec sheet] (an archive page for one early rack delay); other units used 12+1 bit conversion for a "grittier" character [DOCUMENTED] (search result on 13-bit design). General industry pattern of low sample rate for RAM economy is DOCUMENTED but the exact rate varies by unit/year — treat individual figures as illustrative, not universal.
- Modulation on early digital units is typically a simple LFO-varied delay-line read pointer producing chorus/vibrato-like modulation; depth/rate are unit-specific and not independently verified here [FOLKLORE/general knowledge, not directly sourced].

## Modern clean / ping-pong / dual delays

- Clean digital delays are expected to be effectively transparent (full bandwidth, high bit depth, negligible added noise/distortion) with delay times from sub-1 ms up to several seconds, feedback controllable well past 100% with a limiter to avoid runaway clipping, and independent L/R delay times/panning for ping-pong or dual-tap configurations. This is standard-practice description from current product literature rather than a single measured spec [DOCUMENTED by product convention, not independently measured].

## Ducking / sidechain behaviour

- Ducking delays add threshold and amount/depth controls on top of time/feedback/mix, attenuating repeats while the dry signal is present; a cited real-world example used ~2–4 dB of gain reduction on repeats during vocal passages [DOCUMENTED example, not a universal figure] (two mixing tutorials).

## Tempo sync note values

Quarter-note ms = 60000 / BPM. Other values derive from it: half = ×2, whole = ×4, eighth = ÷2, sixteenth = ÷4, thirty-second = ÷8; dotted = straight × 1.5; triplet = straight × 2/3 [MEASURED/arithmetic, DOCUMENTED convention] (two online tempo-to-delay calculators). Example at 120 BPM: quarter = 500 ms, dotted eighth = 375 ms, triplet eighth ≈ 167 ms.

## Target figures table

| Parameter | Target figure | Confidence | Source |
|---|---|---|---|
| Tape wow rate | 0.5–6 Hz | High | AES6-2008 |
| Tape flutter rate | 10–100 Hz | High | AES6-2008 |
| Wow/flutter depth reference | 0.1% ≈ 1.7 cents | Medium | Encyclopedia article on wow |
| Tape head bump (15 ips) | ~50–60 Hz | Medium | Forum thread / engineering article |
| Tape head bump (30 ips) | ~100–120 Hz | Medium | Engineering article |
| Tape self-oscillation threshold | ≥100% feedback | Medium | Pedal makers' articles (folklore-adjacent) |
| BBD delay range (single chip) | 20.5–205 ms | High | 4096-stage BBD chip datasheet |
| BBD chained max delay | ~1.5 s | Medium | Product spec (a chained-BBD pedal) |
| BBD S/N (single chip) | ~75 dB | High | Datasheet |
| Early digital bandwidth | ~10 Hz–16 kHz | Medium | An early rack delay's spec sheet |
| Early digital bit depth | 12-bit (some 12+1) | Medium | Product literature |
| Early digital sample rate | ~26–32 kHz | Low | General/secondhand sourcing |
| Tempo-sync formula | ms = 60000/BPM × modifier | High | Standard music-math, multiple calculators |
| Dotted multiplier | ×1.5 | High | Standard convention |
| Triplet multiplier | ×2/3 | High | Standard convention |
| Ducking gain reduction (typical) | 2–4 dB | Low | Single documented mix example |

**Blocking unknown**: no MEASURED (oscilloscope/spec-sheet-grade) figures were found for modern clean/ping-pong delay feedback ceiling or maximum delay time as a class — these vary per product and are described here only by convention, not by a citable spec.
