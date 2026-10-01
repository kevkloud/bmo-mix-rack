Citation dossier for the early-reflections section of the reverb pack.
Web research on AURORA, 2026-09-20; nothing here has been measured or heard by us.

# Early reflections — psychoacoustics and citations

Where a number is folklore it is flagged with ⚠️. Where the primary source was read directly during this research it is marked **[primary verified]**. Everything else rests on the secondary source named next to it. Worked arithmetic is labelled as arithmetic, not as a citation.

Contents:

1. Precedence / Haas effect
2. Initial Time Delay Gap (ITDG)
3. Early lateral reflections, ASW, spaciousness, LEV
4. Griesinger on early reflections
5. Clarity indices
6. Comb filtering from discrete early taps
7. Distance perception
8. Echo density growth and mixing time
9. ER pattern as a room-size/shape cue; mono compatibility
10. ER time spans by room size, and classic digital structures
11. Consolidated folklore flags
12. Sources to buy or borrow
13. Design reads

---

## 1. Precedence / Haas effect

### 1.1 The three time windows

Taxonomy from Litovsky, Colburn, Yost & Guzman (1999), "The precedence effect," *JASA* 106(4), 1633–1654, DOI 10.1121/1.427914 — https://pubs.aip.org/asa/jasa/article/106/4/1633/557441/The-precedence-effect. Best free restatement with primaries attached: Brown, Stecker & Tollin (2015), *JARO* 16:1–28, open access — https://pmc.ncbi.nlm.nih.gov/articles/PMC4310855/

| Phenomenon | Delay | Primary |
|---|---|---|
| Summing localization (fused image between the two) | **< 1 ms, strongest < 0.5 ms** | Wallach, Newman & Rosenzweig 1949 |
| Median-plane first-wavefront dominance onset | **> ±550 µs** | Blauert 1971, *JASA* 50(2B), 466–470 |
| Localization dominance | **~1–10 ms** | Litovsky & Shinn-Cunningham 2001 |
| Echo threshold, clicks | **~5 ms** (2–10 ms across paradigms) | Bianchi et al. 2013 |
| Echo threshold, broadband noise burst | **4–7 ms** | Litovsky & Shinn-Cunningham 2001 |
| Echo threshold, 500 ms piano tones | **7–8 ms** equal-level → **15–26 ms** with realistic lag attenuation | Hafter et al. 2001 |
| Fusion, complex sounds (speech, piano) | **up to ~40 ms** | Wallach et al. 1949 |

Build-up (Clifton effect): fusion threshold rises from **5–10 ms** for a single lead/lag pair to **15–30 ms+** after several repetitions, and collapses on any abrupt geometry change (Clifton et al. 1987–91; Freyman, Clifton & Litovsky 1991). Neural lag recovery ~10 ms in inferior colliculus, ~20 ms in cortex. Children around 5 years show echo thresholds of 25–45 ms against under 15 ms for adults (Clifton et al. 1984; Litovsky & Godar 2010).

Concise modern overview: Zahorik & Neal, "Reflected Sound: Friend or Foe?", *Acoustics Today* 18(4), Winter 2022 — https://acousticstoday.org/wp-content/uploads/2022/11/Reflected-Sound-Friend-or-Foe-Pavel-Zahorik-and-Matthew-T.-Neal.pdf **[primary verified]**. It gives the simplified ladder for impulsive signals: summing localization below 1 ms, precedence 1–5 ms, separate images above 5 ms, with longer signals needing larger delays for the same effects.

⚠️ Unverified but standard: the ladder "10 ms impulsive / 50 ms speech / 80 ms orchestral music" for echo threshold and the localization blur figure of ±3.6° straight ahead are both universally attributed to Blauert, *Spatial Hearing* (MIT Press, rev. ed. 1997). The book could not be opened to confirm page or wording.

### 1.2 Haas 1951 — the paper was read, and the folklore is wrong in a specific way

Haas, H. (1951), "Über den Einfluß eines Einfachechos auf die Hörsamkeit von Sprache," *Acustica* 1(2), 49–58 (his 1949 Göttingen dissertation); English: "The Influence of a Single Echo on the Audibility of Speech," *JAES* 20(2), 146–159 (1972), trans. Ehrenberg. Free scan: https://www.effectrode.com/wp-content/uploads/2025/07/The_Influence_of_a_Single_Echo_on_the_Audibility_of_Speech_Helmut_Haas_1949.pdf **[primary verified — all numbers below are from the paper's own text]**

Conditions: two matched loudspeakers, **3 m from the observer, at ±45°**; measurements on the roof of the Institute (free field), plus rooms of RT ≈ 0.8 s and 1.6 s. Continuous text at **5.3 syllables/s** for all tests (speed itself varied over **3.5 / 5.3 / 7.4 syll/s**). Primary loudspeaker at ~55 phon.

- **The "10 dB" claim, verbatim:** "for differences from 5 to 30 ms the intensity of the echo loudspeaker must be ten times greater than that of the primary loudspeaker, i.e., against 10 dB, in order to create the impression of equal loudness. Above 30 ms the intensity difference drops slightly." So the 10 dB is a **5–30 ms equal-*loudness* balance**, not a licence to add +10 dB inaudibly.
- **Echo disturbance** (percentage of listeners who "felt disturbed"): at 5.3 syll/s and equal intensities, 10–20 % disturbed at **40–50 ms**, **50 % at 68 ms**, 100 % at ~100 ms. Haas's own summary: "an admissible delay difference of 45 ms and a disturbing difference of 85 ms," with 68 ms as the characteristic value.
- **Level dependence (the most useful number for a reverb):** critical delay = **68 ms at 0 dB → 108 ms at −3 dB → 175 ms at −6 dB**, and "echo intensities more than **10 dB** below that of the direct sound do not disturb at all." He notes reducing the echo intensity by only 5 dB doubles the critical difference.
- **Direction of the echo had no effect** beyond the scatter. **Timbre did:** HF-rich echoes are markedly more disturbing; rolling off the echo's highs raises the critical delay with almost no loudness penalty. This is the published basis for per-tap lowpass filtering.
- **Absolute level irrelevant:** 55 / 45 / 35 phon gave the same disturbance.
- **Reverberation raises the threshold:** critical delay **78 ms** in the RT ≈ 1.6 s room vs 68 ms free-field.
- Monosyllabic logatom intelligibility could not be impaired by a single echo of any delay.
- He cites Stumpp: critical delay 80 ms for echo and speech from the same lateral direction, 50 ms for opposite lateral directions; and Cremer: amplification by 10 dB or more is feasible without the amplifying source being perceived.

**Folklore to flag:** (a) the "up to 40 ms and still images to the first arrival" figure is **Wallach et al. 1949**, not Haas; (b) Haas measured *echo disturbance for speech*, not localization dominance; (c) precedence collapses when the lag exceeds the lead by roughly **15 dB** (Langmuir/Schodder-era; restated only, original not accessed); (d) Toole: "The much quoted 30 ms (±) as the fusion interval for speech clearly applies **only if the delayed sound has the same level as the direct sound**" (https://audioroundtable.com/misc/Loudspeakers_and_Rooms.pdf); (e) the dissertation is 1949, the *Acustica* paper 1951, the translation 1972 — citing "Haas 1972" alone misleads.

### 1.3 Barron's single-reflection map — better than Haas for a reverb

Barron, M. (1971), "The subjective effects of first reflections in concert halls — the need for lateral reflections," *J. Sound Vib.* 15, 475–494. Single lateral reflection at **40°**, 3 m, anechoic, music. Regions on his Fig. 5 (delay 0–100 ms × lag level −25 to +5 dB):

- **Image shift**: above threshold for delays below **~7–8 ms**, and for levels above ~+2 dB up to 50 ms
- **Spatial impression**: broad region, anchored by his "curve of equal spatial impression" through **40 ms at −6 dB**; spatial impression weakens for delays under 10 ms and was produced for 10–80 ms
- **Tone colouration**: **~10–50 ms** at audible levels
- **Disturbance (echo)**: delays from **~50 ms** upward

Replicated and extended with 11 listeners in 2025: Neidhardt, Surdu, Lladó & De Sena, "Subjective evaluation of the first incoming reflection — revisiting and extending Barron's study," Forum Acusticum 2025, DOI 10.61782/fa.2025.0837 — https://dael.euracoustics.org/confs/fa2025/data/articles/000837.pdf **[primary verified]**. Findings: Barron's diagram came from **only two listeners (himself and his supervisor)**; the echo boundary replicated almost exactly; **image shift was much narrower** than Barron drew (levels above 0 dB and lags below 5–6 ms; nothing beyond ~10 ms); ASW extended **0 to ~45 ms**; colouration confirmed around 10–50 ms but reported by some listeners at much longer delays; thresholds are **strongly signal-dependent** (jazz ensemble vs solo bass guitar) and individual differences were considerable.

---

## 2. Initial Time Delay Gap (ITDG)

**Definition (Beranek 1962, *Music, Acoustics, and Architecture*):** interval between arrival of the direct sound and the first significant reflection, defined **for one specific location near the middle of the main floor** — not a seat-by-seat field. Later Beranek/Takenaka practice used a dodecahedral omni source on stage. **ITDG is not an ISO 3382 parameter.**

**Values (Beranek's tabulated data):** Boston Symphony Hall **15 ms**, Amsterdam Concertgebouw **21 ms**, Vienna Grosser Musikvereinssaal **28 ms**. "Very few halls in the entire list exceed 40 [ms], and none of these are in the excellent list" (http://proaudioencyclopedia.com/comments-on-leo-l-beraneks-three-concert-hall-opera-house-books-and-suspended-sound-reflecting-panels-in-concert-halls/). Opera-house table (Beranek/Takenaka, https://www.av-info.eu/acoustic/concerthall.html): Tokyo Bunka Kaikan 14, Paris Opéra Garnier 15, Budapest Staatsoper 15, La Scala 16, Budapest Erkel 17, Vienna Staatsoper 17, Covent Garden 18, Teatro Colón 18, Dresden Semperoper 20, Komische Oper 20, Amsterdam Muziektheater 32, Deutsche Oper 33, Chicago Civic 41 ms.

⚠️ Some web sources give the Musikverein as ~14 ms. That contradicts Beranek's 28 ms — **do not use it**. Vienna, the top-ranked hall, has the *longest* of the three ITDGs, which is itself the counterexample.

⚠️ "Best ITDG is 12–25 ms" and "< 20 ms intimate, > 35 ms detached" are repeated widely with no primary attribution. Beranek's own language was qualitative: long first delays sounded "arena-like" and "remote".

**ITDG is not a validated intimacy predictor, and Beranek withdrew the claim.** Beranek (2004), "Comments on 'intimacy' and ITDG concepts in musical performing spaces," *JASA* 115, 2403 — he said the word choice "may have been unfortunate" and separated intimacy from ITDG. Documented in Hyde, J. R. (2019), *Acoustics* 1(3), 561–569, DOI 10.3390/acoustics1030032 — https://www.mdpi.com/2624-599X/1/3/32. Supporting: Barron (1993) found little correlation between ITDG and jury "intimacy"; Lokki & Pätynen note ITDG ignores overall level and the spatial location of first reflections and found no correlation with seat-dependent intimacy; Lokki has called it "misleading" (*Acoustics Today*, https://acousticstoday.org/wp-content/uploads/2014/10/Leo-Beranek-and-Concert-Hall-Acoustics.pdf **[primary verified]**). In Beranek's own 58-hall analysis ITDG ranks **last** of BQI, EDT_mid, G125, SDI, ITDG. Later work correlates subjective intimacy with total level G instead.

---

## 3. Early lateral reflections, ASW, spaciousness, LEV

**Barron & Marshall (1981)**, "Spatial impression due to early lateral reflections in concert halls: the derivation of a physical measure," *J. Sound Vib.* 77(2), 211–232, DOI 10.1016/S0022-460X(81)80020-X. Varied reflection delay, direction, level, spectrum; the early lateral energy fraction is **linearly related** to perceived spatial impression.

**ISO 3382-1:2009 definitions** (the standard is paywalled; the free preview holds front matter only **[primary verified]**; formulas cross-checked across three independent transcriptions):

```
J_LF  = ∫(5→80 ms) p_L²(t)dt        / ∫(0→80 ms) p²(t)dt          (Eq. A.14)
J_LFC = ∫(5→80 ms) |p_L(t)·p(t)|dt  / ∫(0→80 ms) p²(t)dt          (Eq. A.15)
L_J   = 10 lg[ ∫(80 ms→∞) p_L²(t)dt / ∫(0→∞) p_10²(t)dt ]  dB      (Eq. A.16)
IACC  = max|IACF(τ)|, −1 ms < τ < +1 ms ;  IACC_E: 0–80 ms ; IACC_L: from 80 ms
```

`p_L` = figure-of-eight with its **null at the source**; `p` = omni; `p_10` = free-field at 10 m. The cos² weighting falls out of squaring the figure-of-eight pressure; the |cos| variant (LFC) is Kleiner (1989), *Applied Acoustics*, and ISO calls it "subjectively more appropriate." The 5 ms lower limit exists to exclude the direct sound (the figure-of-eight null is imperfect) — universal convention, but ISO's own wording for it could not be sourced.

⚠️ Two common errors in secondary sources: the **denominator is plain omni energy**, not a cos-weighted integral; and the **modulus in J_LFC is essential** (without it two mirror reflections cancel to zero). ⚠️ The **IACC_L upper limit is inconsistent** across sources: end-of-IR/∞, 750 ms (Hidaka 1995), 1000 ms, or 500–2000 ms. State the ambiguity rather than picking one.

**BQI = 1 − IACC_E3** — IACC over 0–80 ms, arithmetic mean of **500, 1000, 2000 Hz**. Okano, Beranek & Hidaka (1998), *JASA* 104(1), 255–265, DOI 10.1121/1.423955. Beranek's Heyser lecture: https://aes2.org/wp-content/uploads/2023/08/AES123heyser-Beranek.pdf. Best chamber-music halls: BQI_mid > 0.68.

**ISO 3382-1 Table A.1** (JNDs and typical ranges; third-party transcription, corroborated row by row):

| Quantity | Listener aspect | Bands | JND | Typical range |
|---|---|---|---|---|
| G | Subjective level | 500, 1000 | 1 dB | −2 to +10 dB |
| EDT | Reverberance | 500, 1000 | 5 % rel. | 1.0–3.0 s |
| C80 | Clarity | 500, 1000 | 1 dB | −5 to +5 dB |
| D50 | Clarity | 500, 1000 | 0.05 | 0.3–0.7 |
| Ts | Clarity | 500, 1000 | 10 ms | 60–260 ms |
| J_LF | ASW | 125, 250, 500, 1000 | 0.05 | 0.05–0.35 |
| L_J | Envelopment | 125, 250, 500, 1000 | **not known** | −14 to +1 dB |

C50, LFC, T30 and IACC are **not** Table A.1 rows (IACC is in informative Annex B). The **IACC JND of 0.075** is Cox, Davies & Lam (1993), *Acustica* 79, 27–41 — not ISO. The "C50 JND = 1 dB" figure is an extension by analogy that appears in a room-simulation vendor's table. L_J is the one quantity energy-averaged rather than arithmetic-averaged.

Barron (2005), *Acoust. Sci. Tech.* 26(2), 162–169 — https://www.jstage.jst.go.jp/article/ast/26/2/26_2_162/_pdf — argues ISO's two-octave averaging is too narrow and recommends **LF over 125–1000 Hz (Beranek's LF_E4)** and **C80 over 500–2000 Hz (C80(3))**, on the grounds that "for the early lateral fraction there is significant evidence that low frequencies are important whereas high frequencies are less so." Measured mean LF across 17 British auditoria / 189 positions: **≈ 0.19, virtually frequency-independent**. Barron's recommended mid-frequency ranges for concert halls: RT 1.8–2.2 s, EDT 1.8–2.2 s, **C80 −2 to +2 dB**, **LF 0.10–0.35**, G > 0 dB (https://www.akutek.info/Presentations/MB_Objective_Assessment_Pres.pdf).

**LEV — Bradley & Soulodre (1995)**, "Objective measures of listener envelopment," *JASA* 98(5), 2590–2597, DOI 10.1121/1.413225; companion Soulodre & Bradley, *JASA* 97(4). Finding: **LEV is almost solely produced by lateral energy arriving 80 ms or later**; the measure is late lateral sound level **G_LL (= ISO's L_J)**, measured **−14.1 to +3.4 dB** across their halls (hence ISO's −14…+1 range), and predominantly determined by the hall's total absorption. Review: https://nrc-publications.canada.ca/eng/view/accepted/?id=cccb5316-c1fc-4148-82c7-195517d16e40. The ASW/LEV split was first proposed by **Morimoto & Maekawa (1989)**: ASW = "the width of a sound image fused temporally and spatially with the direct sound image"; LEV = "the degree of fullness of sound images around the listener, excluding a sound image composing ASW."

⚠️ Unsourced despite wide repetition: "LF 0.20–0.30 in good halls" (vendor glossary only), "BQI ≈ 0.66 for Boston/Vienna" (not in any accessible Beranek text), and Bradley & Soulodre's own regression coefficients (abstract publisher-elided).

**Asymmetry worth writing down:** the spatial measures weight **125–1000 Hz**; the clarity measures weight **500–1000/2000 Hz**. A reverb building ASW with broadband early taps is targeting the wrong band.

---

## 4. Griesinger on early reflections

The primaries are online and free, and his position is at odds with the Barron/Beranek orthodoxy. Index: https://www.davidgriesinger.com/

**The headline position**, verbatim from Griesinger, "Phase coherence as a measure of acoustic quality, part one," ICA 2010 Sydney — https://www.davidgriesinger.com/wp-content/uploads/2026/06/DG-Australia-8-10-paper11.pdf **[primary verified]**:

> "excess reflections in the time range of 10ms to 100ms reduce engagement, whether they are lateral or not."

**The mechanism.** He argues localization, timbre, pitch and "proximity" are carried by the **phase coherence of harmonics above ~700–1000 Hz**, analysed in a **100 ms window** starting at the onset of each sound event; reflections scramble those phases. From part one's summary: if the amplitude of the sum of all reflections in that 100 ms window is **at least 3 dB below** the direct sound in the same window, the brain can separate direct from reverberation and timbre and azimuth survive. Useful pitch and azimuth discrimination is available within 20 ms of onset.

**LOC** — his impulse-response measure, from the same paper:

- Counts nerve firings from direct-sound onset (band-limited **700–4000 Hz**) in a **100 ms window**, against firings from reflections in the same window.
- `S` = level 20 dB below the peak of direct plus reverberant, taken as the point where firings cease; `D` = 100 ms window width; a −1.5 dB fudge factor fitted to localization data.
- **LOC = 0 dB is the threshold; LOC = +3 dB is adequate for engagement and localization.**
- Worked hall values (part two, https://www.davidgriesinger.com/wp-content/uploads/2026/06/DG-Australia-8-10-paper-2.pdf **[primary verified]**), all at D/R = −10 dB: **Amsterdam Concertgebouw +6 dB** (more than 35 ms before reflected pressure equals direct), **Boston Symphony Hall +4.2 dB**, **Boston scaled to half its linear dimensions +0.5 dB** — "muddy and there is no engagement," because the faster buildup puts far more energy in the 100 ms window. The two real halls' late reverberation could be swapped with no audible difference; the difference was ~10 ms more initial delay in Amsterdam.
- In a typical hall half the seats have D/R below −10 dB; almost all are below −3 dB. Barron-style single-reflection experiments ran at D/R of +25 to −3 dB, which he argues is typical of recordings, not halls.
- Reference code: https://www.davidgriesinger.com/wp-content/uploads/2026/07/Matlab-code-for-calculating-LOC.txt

**The 5 ms / 7 ms crossover** (his later hall and lab work, reported in "The effects of early reflections on proximity, localization and loudness," IOA 2018, and "Localization, loudness and proximity"): reflections arriving **within ~5 ms** of the direct sound *increase* the likelihood of perceiving proximity and extend the localization limit; reflections arriving **after ~7 ms** *decrease* both. He added a **cross-fade window centred at 6 ms** to the LOC code to model this. (The IOA PDF URL that search engines index now returns 404; the material is on his site as slides.)

**"Direct Sound, Engagement, and Running Reverberance" (2009)** — https://www.davidgriesinger.com/wp-content/uploads/2026/05/Direct-Sound-Engagement-and-Running-Reverberance.pdf **[primary verified]**, the most quotable single source:

- Running liveness "depends both on the audibility of the direct sound and on reflected energy that arrives **at least 100ms** after the direct sound." Reflections that arrive earlier are desirable, "but when there are too many they mask both the direct sound and the desirable late reverberation."
- In classic orchestral recordings "reflections that arrive before about **30ms** are quite rare."
- With syllabic sources most people can localize "even when the total energy in reverberation is more than ten times stronger than the direct sound. But for this to be possible there must be **at least a 20ms delay** before significant reflected energy arrives."
- "It is current acoustic dogma that strong early lateral reflections are the key to a successful hall, but this is not the case when the reflections come too soon and are too strong."
- The ability to separate direct from reverberation is "**at least 4dB better in the 1000Hz octave band** than at other frequencies."
- Foreground/background stream model: separation "is started by the arrival of the first wavefront — the first 20ms or so of sound that arrives before reflections overcome it." Where separation fails, foreground and background blend into one frontal, non-enveloping stream.
- In a small hall, blocking strong prompt reflections from the stage back wall and side walls with a few absorbing panels increased both clarity and the sense of the hall despite a slightly shorter RT.

**The ASW/envelopment time model** — Griesinger (1997), "The psychoacoustics of apparent source width, spaciousness and envelopment in performance spaces," *Acta Acustica* 83(4), 721–731, and "Objective measures of spaciousness and envelopment," AES 16th Int. Conf. — https://www.akutek.info/Mitt%20Bibliotek/GRIESINGER%20Objective%20measures%20of%20spaciousness%20and%20envelopmentobjmeas.pdf **[primary verified]**:

- **ESI (Early Spatial Impression):** lateral energy within **50 ms of the *end* of a note**. Bound to the source, **not** enveloping, "the spatial impression is that of a small room." Depends on medial/lateral ratio, not absolute level.
- **CSI (Continuous Spatial Impression):** any delay greater than **~10 ms**; fully enveloping; ratio-dependent.
- **BSI (Background Spatial Impression):** "There is a perceptual inhibition that occurs in the background stream just after 50ms. This inhibition is gradually released, such that background sounds arriving **150ms or so** after the ends of the notes are strongly audible." BSI strength is **absolute** (scales with source level), typically ~6 dB stronger than CSI at classical listening levels.
- Minimum delay for interaural fluctuations ≈ **0.5 / bandwidth**; a 100 Hz critical band therefore needs **~5 ms**.
- A single lateral reflection at **−10 dB produces 6 dB fluctuations in IID**.
- Low-frequency interference: in the 63 Hz octave, a single lateral reflection at **5.5 ms** is very wide and enveloping; at **13 ms** it is nearly monaural; **beyond 20 ms all delays sound about the same**.
- Envelopment fluctuations "that follow the ends of sounds by **at least 160ms** are the most effective"; equal-reverberant-loudness impulse responses cross at **~350 ms**; his RR160 measure integrates **160–320 ms**; he argues 160 ms is a better start than 80 ms for the late lateral integral.
- Orchestral-music masking slope: **1 dB change in D/R → 4 dB change in reverberant audibility**.
- A 12′×15′×9′ room (RT 0.2 s, time constant 30 ms) "will not be enveloping with a single sound source" — too little delay to generate interaural fluctuations; only ESI is available.
- Optimum source angles for envelopment: 90° below 700 Hz, moving toward the medial plane above; about 150° for broadband energy above 2 kHz.

**On the 50–150 ms region**, from a magazine interview about his surround reverb algorithm work — https://www.soundonsound.com/people/david-griesinger-lexicon-creating-reverb-algorithms-surround-sound: "Between 50mS and 120mS is probably the worst possible time to get energy from an intelligibility point of view." His design strategy there: strong reflections **before 50 ms** create distance without harming intelligibility; a flatter, low-level profile out to **160 ms**; exponential decay after that for reverberance and envelopment. Removing the 50–150 ms energy entirely leaves an audible gap in reverberation onset.

**On clarity metrics:** his 2007 ICA Madrid and 2013 ICA "What is Clarity and how can it be measured?" both start from the observation that halls with very similar measured RT, EDT and C80 sound quite different.

**Counterweight — this is contested.** Pätynen, Tervo, Robinson & Lokki (2014), "Concert halls with strong lateral reflections enhance musical dynamics," *PNAS* 111, DOI 10.1073/pnas.1319976111 — binaural gain for lateral arrival is **~1–5 dB higher than frontal from 2 kHz to above 10 kHz**, so strong early lateral reflections amplify perceived musical dynamics. Lokki, Pätynen, Kuusinen & Tervo (2012, *JASA*), "Disentangling preference ratings of concert hall acoustics using subjective sensory profiles" — twenty assessors, nine seats in three halls; **"Proximity"** emerged as an elicited attribute alongside Envelopment/Loudness, Reverberance, Bassiness, Definition and Clarity. That is independent support for Griesinger's construct even where the prescription differs.

---

## 5. Clarity indices

**Origins.** D50 / *Deutlichkeitsgrad*: **R. Thiele (1953)**, "Richtungsverteilung und Zeitfolge der Schallrückwürfe in Räumen," *Acustica* 3, 291–302 — built on the Haas/precedence window. C80 / *Klarheitsmaß*: **Reichardt** et al., Dresden, ~1970–75 (*Z. elektr. Inform.- u. Energietechnik* 5, 144, 1975).

**Formulas:**

```
C_te = 10·lg[ ∫(0→te) p²dt / ∫(te→∞) p²dt ]  dB      te = 50 or 80 ms
D_te =        ∫(0→te) p²dt / ∫(0→∞) p²dt
Ts   =        ∫(0→∞) t·p²dt / ∫(0→∞) p²dt
```

**Exact identity:** `C_te = 10·lg[ D_te / (1 − D_te) ]`, so **D50 = 0.5 ⇔ C50 = 0 dB**. C50 and D50 are the same quantity in different coordinates, not two independent targets. ⚠️ One measurement-software manual prints `D50 = C50/(C50+1)`, true only if C50 is read as a linear ratio, not in dB.

**Rationale for the two limits:** 50 ms because speech syllables are shorter than musical notes and intelligibility depends on catching each syllable onset (Thiele, from the Haas window); 80 ms because the auditory system integrates roughly that long before registering a new event.

**Targets, sourced vs not:**

| Claim | Status |
|---|---|
| **C80 −2 to +2 dB** for concert halls, mid-frequencies | **Sourced** — Barron 2005 Table 3 and his lecture material |
| Measured C80(3) in top halls: **Vienna −2.0, Amsterdam −0.97, Boston −0.7 dB** | **Sourced** (Beranek). These sit at or *below* the bottom of Barron's band — higher C80 is not "better" |
| **C50 > 0 dB / D50 > 0.5** for adequate speech definition | **Sourced** (Reichardt tradition; D50 > 0.5 frequency-independent criterion, https://pmc.ncbi.nlm.nih.gov/articles/PMC7065602/) |
| C50 ≈ **3–4 dB** for cinemas; C50 **−3 to +2 dB** for adequate intelligibility | Sourced to the German Reichardt literature |
| "C50 > +2 dB", genre-by-genre C80 tables (orchestral −4…+1, opera −2…+2, chamber −1…+3, cinema +3…+6), "D50 > 0.6 for classrooms / > 0.7 for broadcast" | ⚠️ **FOLKLORE** — vendor blogs citing no papers |
| "C80 −5 to +5 dB is the target" | ⚠️ **Misuse** — that is ISO's *typical measured range*, not a recommendation |

⚠️ The **C80 JND of 1 dB has been challenged** in the re-measurement literature (as has EDT's 5 %, re-measured at ~18 %). Present them as the standard's declared values, not settled psychoacoustics.

**Early reflections help speech:** Bradley, Sato & Picard (2003), "On the importance of early reflections for speech in rooms," *JASA* 113(6), 3233–3244 — https://pubs.aip.org/asa/jasa/article/113/6/3233/548721 — adding early reflections raises the **effective signal-to-noise ratio** and intelligibility for impaired and non-impaired listeners; **useful/detrimental ratios based on an 0.08 s early window** predicted intelligibility most accurately.

---

## 6. Comb filtering from discrete early taps

### 6.1 The math (derivable, not a finding)

Direct plus one copy of relative amplitude `a` at delay `t`:

```
H(f) = 1 + a·e^(−j2πft)
|H(f)| = √(1 + a² + 2a·cos 2πft)
peaks at f = n/t (gain 1+a); notches at f = (2n+1)/(2t) (gain 1−a)
first notch = 1/(2t);  notch spacing = peak spacing = 1/t Hz
peak-to-notch depth (dB) = 20·log10((1+a)/(1−a))
```

| Reflection re direct | Peak | Notch | Peak-to-notch |
|---|---|---|---|
| 0 dB | +6.02 | −∞ | ∞ |
| −3 dB | +4.65 | −10.69 | **15.3 dB** |
| **−6 dB** | +3.53 | −6.04 | **9.6 dB** |
| −10 dB | +2.39 | −3.31 | **5.7 dB** |
| −20 dB | +0.83 | −0.91 | **1.74 dB** |
| −30 dB | +0.27 | −0.28 | **0.55 dB** |
| −40 dB | +0.09 | −0.09 | **0.17 dB** |

(Arithmetic from the formula, not a citation.) Below about −20 dB, depth(dB) ≈ 17.37 × 10^(L/20). Notch count below 10 kHz = 10000·t: a 1 ms tap puts 10 notches in band (first at 500 Hz); a 10 ms tap puts 100 (first at 50 Hz); a 25 ms tap puts 250. Long taps produce ripple too fine to resolve as timbre and are heard as roughness or echo instead.

EBU Tech 3276 makes the link explicitly: "The effect of early reflections may also be observable, as a comb filter effect, in the operational room response curve." **[primary verified]**

### 6.2 Published audibility thresholds

**Olive & Toole (1989)**, "The Detection of Reflections in Typical Rooms," *JAES* 37(7/8), 539–553. AES e-lib https://www.aes.org/e-lib/browse.cfm?elib=6079; full PDF https://pearl-hifi.com/06_Lit_Archive/15_Mfrs_Publications/Harman_Int'l/AES-Other_Publications/Reflections_in_Normal_Rooms.pdf

Body-text numbers (the threshold-vs-delay curves are **in the figures only** — ⚠️ graph-reads must be checked against the plots before citing):

- **Detection → image-shift gap: 12.3 dB anechoic, 8 dB in the relatively reflection-free room, 7 dB in a normal room.**
- **Detection → spaciousness gap: ~6 dB** (Seraphim's estimate, endorsed).
- Percepts above threshold: **image spreading < ~10 ms; spaciousness and image spreading ~10–40 ms; spaciousness and an identifiable echo beyond.**
- **Room effect:** up to ~30 ms delay thresholds change little from anechoic to the reflection-free room and rise **no more than ~6 dB** in the IEC room; above 30 ms they rise sharply with each more reflective space.
- **Reverberation on the source signal:** below ~10 ms it **increases** sensitivity; above ~20 ms it **elevates thresholds by 20–30 dB or more**; the effect saturates above RT ≈ 0.3 s.
- **Angle:** reflections from directions near the source can be **5–10 dB louder** before detection.
- **Forward masking:** a 20 ms, −4 dB reflection raised the detection threshold ~+8 dB anechoically, and the effect was "virtually nonexistent" in a normal room.
- Signal type: "At short time delays, below about 10 ms, the continuous sound was more revealing, and at longer delays the discontinuous sound was much more revealing."
- Low-passing the reflection down to 500 Hz left absolute thresholds close to broadband.

Companion: Toole & Olive (1988), "The Modification of Timbre by Resonances," *JAES* 36(3), 122–142 — resonances are **more** detectable in reverberant rooms, the opposite sign to the > 20 ms reflection result.

**Bech's small-room series:** Bech (1995), *JASA* 97(3), "Timbral aspects of reproduced sound in small rooms I" (direct + **17 individual reflections** + reverberant field; **first-order floor and ceiling reflections individually contribute to speech timbre**; for noise, the left sidewall too); Bech (1996), *JASA* 99, 3539–3549 (part II); Bech (1998), *JASA* 103, 434–445, "Spatial aspects of reproduced sound in small rooms."

**Most directly on-point for a reverb:** Brunner, Maempel & Weinzierl, "On the Audibility of Comb Filter Distortions," 24th Tonmeistertagung 2006 / AES 122nd — https://www2.users.ak.tu-berlin.de/akgroup/ak_pub/2007/Brunner%20Maempel%20Weinzierl%202007_On%20the%20audibility%20of%20comb%20filter%20distortions%20TMT.pdf. Delays 0.1–15 ms; piano, snare roll, speech. Thresholds expressed as how far below the direct the copy can sit and still be detected: **piano ≈ 13.2 dB (individuals to 21.5 dB, most sensitive near 0.8 ms), snare ≈ 18.2 dB (individuals to 27 dB), speech ≈ 16/22 dB at 15 ms with no minimum found.** Conclusion: comb-filter distortion is audible with the first reflection **more than 20 dB below** the direct. Optimal stimulus duration for detection: 3 s. ⚠️ Moderate confidence on the exact per-stimulus pairings.

### 6.3 Coloration, "boxy", metallic

- **Repetition pitch = 1/τ** — Bilsen (1967/68), *Acustica* 19, 27–32, and his 1968 TU Delft thesis *On the Interaction of a Sound with its Repetitions*. So 2.9 ms → ~344 Hz, 5.8 ms → ~172 Hz, 11.6 ms → ~86 Hz.
- **Coloration threshold shape:** Atal, Schroeder & Kuttruff (1962, 4th ICA, paper H31) and Bilsen (1968) both found the **lowest thresholds at delays of 2–5 ms**, rising for shorter and longer delays (restated at https://www.akustinenseura.fi/wp-content/uploads/2013/08/o23.pdf). ⚠️ **The actual dB values are in figures that could not be retrieved. The circulating "≈ −20 dB coloration threshold attributed to Bilsen" is unsourced.**
- **Halmrast (2001), "Sound Coloration from (Very) Early Reflections"** — https://www.akutek.info/Papers/TH_Coloration2001.pdf: the **"box-Klangfarbe" window is Δt ≈ 5–20 ms**, i.e. comb tooth spacing ~50–200 Hz. Frontal reflections colour most around 5 ms; lateral around 10–20 ms. Quotes Kuttruff's speech threshold **ΔL ≅ −0.6·t₀ − 8 dB** (t₀ in ms) → −14 dB at 10 ms, −20 dB at 20 ms, −26 dB at 30 ms — ⚠️ quoted via Halmrast, and it clearly over-predicts past ~30 ms.
- **Binaural decoloration (Zurek 1979, *JASA* 66(6), 1750–1757):** below 5–10 ms, thresholds for a **diotic** echo are ~**10 dB lower** than dichotic. A reflection identical at both ears colours ~10 dB more audibly than one carrying an interaural difference.
- Halmrast's observation: the Vienna Musikverein shows no boxiness despite reflections throughout 0–25 ms — attributed to distributed/diffuse rather than discrete reflections.
- Salomons, A. M. (1995), "Coloration and Binaural Decoloration of Sound due to Reflections," PhD thesis, TU Delft — http://resolver.tudelft.nl/uuid:7f0331e3-bc1a-4d7f-8d2a-eb5d6cc04fbf (full text not retrievable).

### 6.4 What reduces coloration — published

- **Echo density:** Schroeder (1962) — **~1000 echoes/s** for flutter-free reverberation (see §8; the "10,000/s" figure is Griesinger's, not Schroeder's).
- **Spectral density:** Schroeder — **~15 large response peaks per 100 Hz at T60 = 1 s**; one 40 ms comb gives only 4, so **3–4 parallel combs with incommensurate delays** are needed. Loop gain ≤ 0.85 (−1.4 dB). https://ccrma.stanford.edu/~jos/pasp/Schroeder_Reverberators.html **[primary verified]**
- **Incommensurate, not prime.** What is published is **mutually prime / no common factors**. ⚠️ Dattorro's own appendix quotes Barry Blesser on how the delay values in classic hardware reverberators were chosen: "The notion of delay time selections was **random** in that we just picked a bunch of numbers and there was **no mathematical basis**." The "prime numbers are required" story is folklore.
- **Per-tap / per-line lowpass:** Jot & Chaigne (1991), AES 90th, preprint 3030 — absorbent filters per delay line so every line has the **same frequency-dependent decay rate**, which removes the metallic ring from modes that decay too slowly. Haas's finding that HF-rich echoes are disproportionately disturbing is the perceptual justification.
- **Diffusion via allpass:** Dattorro (1997), *JAES* 45(9), 660–684 — https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf: "one must achieve a balance between eigentone density and echo density"; optimum allpass diffusion coefficient "closer to 0.51" than the extremes. He explicitly rejects colourlessness as the goal: some of the most sought-after commercial reverberators are somewhat coloured.
- **Tap density, with a number:** velvet noise. **Karjalainen & Järveläinen (2007), "Reverberation modeling using velvet noise," AES 30th Conf.**; restated in Välimäki's AES 60th keynote, https://www.dreams-itn.eu/uploads/files/Valimaki-AES60-keynote.pdf **[primary verified]**: velvet noise is smoother than Gaussian noise when pulse density is **≥ 2000 pulses/s**. **Lowpass-filtered velvet noise needs less density** — lowpassed at 1.5 kHz, **600 pulses/s** already sounds smoother than full-band Gaussian noise ("Dark Velvet Noise," DAFx-20, https://dafx2020.mdw.ac.at/proceedings/papers/DAFx20in22_paper_31.pdf **[primary verified]**). Grid-based pulse placement (one jittered pulse per equal window) sounds smoother than fully random placement at the same density.

⚠️ **Counter-intuitive published result:** Robinson, Walther, Faller & Braasch (2013), "Echo thresholds for reflections from acoustically diffusive architectural surfaces," *JASA* 134(4), 2755–2764 — **diffusion weakens echo suppression; diffuse reflections were detected at lower levels than specular reflections of the same total energy**, mainly due to temporal rather than directional diffusion. "Diffusion always hides reflections" is not supported. (Gist via search indexing and a 2021 *Acta Acustica* follow-up; full texts not opened.)

---

## 7. Distance perception

**The power law.** Zahorik, Brungart & Bronkhorst (2005), "Auditory distance perception in humans: a summary of past and present research," *Acta Acustica* 91(3), 409–420. Form `r′ = k·r^a`. **Meta-analysis of 84 data sets from 21 studies: mean a = 0.54, mean k ≈ 1.3, and no data set exceeded a = 0.9.** Zahorik's own virtual-acoustics study: a = 0.39. Sources closer than ~1 m are over-estimated, everything beyond is increasingly under-estimated. ⚠️ The commonly quoted "a ≈ 0.4–0.8" is a reasonable band but **0.54 mean / ≤ 0.9 max** is what is published. Open-access review: Kolarik, Moore, Zahidi & Pardhan (2016), *Atten. Percept. Psychophys.* 78, 373–395 — https://link.springer.com/article/10.3758/s13414-015-1015-1

**Intensity.** ~**6 dB per doubling of distance** in the free field. Kolarik et al.: "Level is a relative distance cue, as distance judgments made solely on the basis of level may be confounded by variation in the level at the source." Zahorik (2002), *JASA* 111(4), 1832–1846: listeners weight **intensity more heavily than DRR**.

**DRR.** Origin: Mershon & King (1975), *Percept. Psychophys.* 18, 409–415, DOI 10.3758/BF03204113. Bronkhorst & Houtgast (1999), "Auditory distance perception in rooms," *Nature* 397, 517–520 — https://www.nature.com/articles/17374 — perceived distance depends on the direct/reflected energy ratio; their model uses a **modified DRR with a 6 ms integration window for the direct-sound energy**, and explains the **auditory horizon**. D/R decreases with distance and is independent of source power (Zahorik & Neal 2022).

**ER-to-direct ratio vs the full tail:** Bronkhorst & Houtgast's 6 ms window is the published answer — **everything after 6 ms, including the early reflections, counts on the reverberant side**. No study was found isolating an early-reflection-to-direct ratio as a distance cue separately from the late tail. Exposing that distinction is an engineering choice, not a published result. Griesinger's 5 ms / 7 ms crossover (§4) is the closest published claim in that direction and is his alone. His anecdote from opera-house electroacoustic work: raising reverberation above 1 kHz by 1 dB moved a singer's apparent distance back by about 3 m.

**DRR resolution.** Larsen, Iyer, Lansing & Feng (2008), *JASA* 124(1), 450–461 — https://pubmed.ncbi.nlm.nih.gov/18646989/: **JND ≈ 2–3 dB at 0 and +10 dB DRR, ≈ 6–8 dB at −10 and +20 dB**; equivalent overall-level change ≈ 1, 1.5, 3, 8 dB at −10, 0, +10, +20 dB DRR; discrimination rests **primarily on spectral cues**. Zahorik (2002), *JASA* 112(5), 2110–2117 gives a flatter **5–6 dB for DRR between 0 and 20 dB**. DRR is roughly an order of magnitude coarser than the ~1 dB intensity JND.

⚠️ **"Auditory horizon ≈ 15 m" is unsourced.** Kolarik et al. 2016: "the limit imposed by the auditory horizon has not yet been measured directly." The 15 m figure that circulates is a different claim — their statement that spectral shape becomes a usable distance cue for sources more than 15 m away.

**Air absorption.** ISO 9613-1:1993 (50 Hz–10 kHz, −20…+50 °C, 10–100 % RH, 101.325 kPa). ⚠️ **Table 1 at 20 °C could not be obtained** — the free preview stops before the 20 °C block. Do not trust second-hand "ISO 9613-1 Table 1" figures. Formula summary: https://gorbatschow.github.io/SonarDocs/sound_absorption_air_iso.en/

Verifiable: **ISO 9613-2:1996 Table 2**, octave-band α in dB/km (https://puc.sd.gov/commission/dockets/electric/2019/el19-003/KMExhibit9.pdf):

| T / RH | 63 | 125 | 250 | 500 | 1 k | 2 k | 4 k | 8 k |
|---|---|---|---|---|---|---|---|---|
| 10 °C / 70 % | 0.1 | 0.4 | 1.0 | 1.9 | 3.7 | 9.7 | 32.8 | 117 |
| **20 °C / 70 %** | 0.1 | 0.3 | 1.1 | 2.8 | 5.0 | 9.0 | 22.9 | 76.6 |
| 30 °C / 70 % | 0.1 | 0.3 | 1.0 | 3.1 | 7.4 | 12.7 | 23.1 | 59.3 |
| 15 °C / 20 % | 0.3 | 0.6 | 1.2 | 2.7 | 8.2 | 28.2 | 88.8 | 202 |
| **15 °C / 50 %** | 0.1 | 0.5 | 1.2 | 2.2 | 4.2 | 10.8 | 36.2 | 129 |
| 15 °C / 80 % | 0.1 | 0.3 | 1.1 | 2.4 | 4.1 | 8.3 | 23.7 | 82.8 |

Same data in **dB per 100 m**:

| | 500 Hz | 1 kHz | 2 kHz | 4 kHz | 8 kHz |
|---|---|---|---|---|---|
| 20 °C / 70 % RH | 0.28 | 0.50 | 0.90 | 2.29 | 7.66 |
| 15 °C / 50 % RH | 0.22 | 0.42 | 1.08 | 3.62 | 12.9 |
| 15 °C / 20 % RH | 0.27 | 0.82 | 2.82 | 8.88 | 20.2 |

Caveats: these are **octave-band** values taken at the lower third-octave centre in the 1–8 kHz bands, so they are **not interchangeable with pure-tone values** (Rindel 2024, *Appl. Sci.* 14(22), 10139, DOI 10.3390/app142210139). Humidity dependence is **non-monotonic** and the peak humidity is frequency-dependent (~4 % RH at 1 kHz, ~10 % at 4 kHz, ~19 % at 10 kHz, 20 °C) — do not model it as "drier = brighter." Measurement basis: Harris (1966), *JASA* 40(1), 148–159 — https://ccrma.stanford.edu/~jos/HarrisJASA66.pdf

**Scale check:** at 20 °C / 70 %, 8 kHz loses 0.77 dB at 10 m, 2.3 dB at 30 m, 7.7 dB at 100 m. Air absorption on the direct path inside a room is negligible; it matters on the **cumulative path length of the tail** — a 2 s tail is ~700 m of travel. Moorer's worked example: for Boston Symphony Hall a 1.7 s reverberation implies ~600 m of travel, which at 40 % humidity means **4 kHz is attenuated ~60 dB more than 1 kHz**.

---

## 8. Echo density growth and mixing time

**The t² law:**

```
dN/dt = 4πc³t² / V   [reflections/s]      N(t) = (4/3)πc³t³ / V
```

Canonical reference: Kuttruff, *Room Acoustics*, geometrical-acoustics chapter (image-source counting). ⚠️ No equation number could be verified — cite the book, not an equation.

Worked values (arithmetic; c = 343 m/s, so 4πc³ = 5.071 × 10⁸):

| V (m³) | 20 ms | 50 ms | 100 ms | t to 1,000/s | t to 10,000/s |
|---|---|---|---|---|---|
| 100 (control room) | 2,028/s | 12,678/s | 50,710/s | **14.0 ms** | 44.4 ms |
| 1,000 (live room) | 203/s | 1,268/s | 5,071/s | **44.4 ms** | 140 ms |
| 20,000 (hall) | 10.1/s | 63.4/s | 254/s | **199 ms** | 628 ms |

This table says how long the ER section must run before a statistical tail can take over without an audible density step. Caveat: the law assumes specular reflection in a convex enclosure — diffusers get there sooner, coupled volumes later.

**Schroeder's number, verbatim** (Schroeder 1962, "Natural Sounding Artificial Reverberation," *JAES* 10(3), 219–223 — https://hajim.rochester.edu/ece/sites/zduan/teaching/ece472/reading/Schroeder_1962.pdf): "approximately 1,000 echoes per second are required for a flutter-free reverberation." ⚠️ **The "10,000 echoes/s" figure is Griesinger's, via Jot & Chaigne 1991**, repeated by Gardner and Julius Smith ("for impulsive sounds, 10,000 echoes per second or more may be necessary for a smooth response," https://ccrma.stanford.edu/~jos/book2000/Desired_Qualities_Late_Reverberation.html). Anything attributing 10,000/s to Schroeder is wrong. "~1 echo per ms" is the same 1,000/s restated.

Companion mode-density criterion — Schroeder & Logan (1961), "'Colorless' Artificial Reverberation," *JAES* 9(3): **0.15 modes/Hz adequate at T60 = 1 s**, scaling with T60, which becomes the delay-network sizing rule **M ≥ 0.15 · T60 · f_s** total delay samples (https://ccrma.stanford.edu/~jos/pasp/Mode_Density_Requirement.html).

**Schroeder frequency:** `f_s = 2000·√(T60/V)` (SI). The original 1954/62 criterion demanded **ten-fold modal overlap → 4000**; Schroeder (1996), "The 'Schroeder frequency' revisited," *JASA* 99(5), 3240–3241, revised it to **three-fold overlap → 2000**. Derivation check (arithmetic): modal density 4πVf²/c³, modal bandwidth 2.2/T60; overlap = 3 gives 2092, overlap = 10 gives 3820.

| Space | V (m³) | T60 (s) | f_s (2000) | f_s (4000) |
|---|---|---|---|---|
| Project studio | 60 | 0.35 | 153 Hz | 306 Hz |
| Control room | 100 | 0.30 | 110 Hz | 219 Hz |
| Tracking room | 300 | 0.60 | 89 Hz | 179 Hz |
| Mid-size hall | 10,000 | 1.8 | 27 Hz | 54 Hz |
| Large hall | 20,000 | 2.0 | 20 Hz | 40 Hz |

A hall has **no modal region in the audio band**. A control room is modal to ~110–150 Hz — exactly where a delay network's own modes will be individually audible as ringing pitches.

**Mixing time.** Polack's rule `t_mix ≈ √V ms` (V in m³) — J.-D. Polack, PhD thesis, Université du Maine, 1988 (not online; cite as a thesis). Values: 10 ms (100 m³), 32 ms (1,000), 100 ms (10,000), 141 ms (20,000). It diverges from the "time to 1,000 echoes/s" column for large V; the two criteria are not the same thing.

Best modern source with perceptual validation: **Lindau, Kosanke & Weinzierl (2012), *JAES* 60(11), 887–898**, open access — https://www.aes.org/e-lib/browse.cfm?elib=16633. Predictors for perceptual mixing time **t_mp50** and the more critical **t_mp95**; **√V explains R² = 78.6 % of variance, V alone 77.4 %** — Polack's functional form is right. ⚠️ The regression constants could not be extracted; open the paper rather than trusting circulated values.

**Normalized Echo Density** — Abel & Huang (2006), AES 121st, paper 6985; author copy https://ccrma.stanford.edu/courses/318/mini-courses/rooms/mus318_Abel_Lecture/echo%20density.pdf

```
η[n] = (1/erfc(1/√2)) · Σ_τ w[τ]·1{|h[τ]| > σ_n},   σ_n = √(Σ_τ w[τ]·h²[τ])
```

w is typically a **20 ms** window; `erfc(1/√2) = 0.3173` is the Gaussian fraction outside ±1σ, which is why NED → 1 for a fully mixed field. **Mixing time = first time NED reaches 1.** Actionable numbers from Huang & Abel (2007), "Reverberation Echo Density Psychoacoustics," AES 123rd: **listeners consistently reported changes in echo-pattern character at NED = 0.3 and NED = 0.7.**

For delay-network designers: Schlecht & Habets (2017), "Feedback Delay Networks: Echo Density and Mixing Time," *IEEE/ACM TASLP* 25(2) — https://ieeexplore.ieee.org/document/7763827/ — echo density is a polynomial in time whose coefficients follow from the delay-line lengths, so a **target mixing time can be achieved by choosing the mean delay length**.

---

## 9. ER pattern as a room-size/shape cue; mono compatibility

### 9.1 What the ER pattern physically encodes

Dokmanić, Parhizkar, Walther, Lu & Vetterli (2013), "Acoustic echoes reveal room shape," *PNAS* 110(30), 12186–12191, DOI 10.1073/pnas.1221464110 — **first-order echoes provide a unique description of a convex polyhedral room** under mild conditions, recoverable from a few microphones. The information is there in the ER pattern.

### 9.2 What listeners use — mostly not the ER pattern

- **Hameed, Pakarinen, Valde & Pulkki (2004), "Psychoacoustic Cues in Room Size Perception," AES 116th Convention, paper 6084** — https://research.aalto.fi/en/publications/psychoacoustic-cues-in-room-size-perception/. RT values of **0.62 / 0.73 / 0.83 s produced increasing perceived room size**, while varying **DRR between −23, −25 and −28 dB produced no change**.
- **Pop & Cabrera (2005), "Auditory Room Size Perception for Real Rooms," Acoustics 2005, Busselton** — https://www.acoustics.asn.au/conference_proceedings/AAS2005/papers/68.pdf **[primary verified]**. 17 blindfolded listeners, three real rooms (**15, 124, 188 m³**), speech. Physical size significantly affected perceived size (ANOVA f = 24.7, p < 0.0001); **source distance also raised it** (f = 16.5, p < 0.0001). The acoustic correlates that worked were **A-weighted stimulus level (negative) and mid-frequency C80 (negative)**; a two-parameter regression on those **predicted 88 % of the variance**. On early reflections, their own conclusion: effects of "the early reflection sequence or the fine frequency response … remain to be investigated."
- Earlier: Sandvad (1999) — some listeners used DRR, others RT; Mershon et al. (1989) — longer RT and greater distance give larger judged size; Cabrera et al. (2005) — clarity index a good predictor, with an inverted-U as volume grows at fixed RT.
- **A sensitivity figure for ER alone:** in the P-Reverb study (arXiv:1902.06880 **[primary verified]**), listeners hearing **direct + early reflections only** (first 80 ms) discriminated cube rooms at a mean-free-path JND of **0.06 m on 2 m — about 3 %**; with the full impulse response the JND fell to ~0.02 m (1 %). The ER pattern carries size information, but the full response carries more.
- **Specular vs diffuse ER:** Robinson, Pätynen & Lokki (2013), "The effect of diffuse reflections on spatial discrimination in a simulated concert hall," *JASA* 133(5), EL370 — simulated hall with 11 early reflections; spatial discrimination was more accurate with specular early reflections. (Abstract paywalled; gist from indexing only.)
- Julius Smith, *PASP*, "Early Reflections" — https://ccrma.stanford.edu/~jos/pasp/Early_Reflections.html **[primary verified]**: early reflections span "the first 100ms or so," ideally up to where the response reaches its asymptotic statistical behaviour; implemented as tapped delay lines; "taps on the TDL may include lowpass filtering for simulation of air absorption"; they influence spatial impression and the perceived **shape** of the space. Qualitative, not quantitative.
- ⚠️ On **room shape**: no listening study was found establishing that listeners recover room shape from the ER pattern. The PNAS result is about algorithms, not perception. Treat as open.

### 9.3 Mono compatibility of ER patterns

⚠️ **No meaningful published psychoacoustic literature on this specific question was found**, only engineering practice. What can be stated rigorously:

- **The math.** Summing L and R annihilates the side component exactly: `(M+S) + (M−S) = 2M`. Anti-correlated content cancels; correlated content adds coherently at +6 dB while uncorrelated content adds at +3 dB. A stereo ER pattern whose channels carry the same signal at different delays comb-filters in mono; one that is anti-phase disappears.
- **Why this specifically kills the spatial effect.** ISO 3382-1 measures lateral energy with a **figure-of-eight microphone whose null points at the source** — a physical difference-taking pickup. The content that produces LF, low IACC and ASW is by construction the content with an inter-channel *difference*, which is what a mono sum removes. IACC → 1; the LF analogue → 0.
- **The one published construction that is exactly mono-flat:** complementary comb filters — Lauridsen (Danish Radio, 1954) and **Schroeder, M. R. (1958), "An artificial stereophonic effect obtained from a single audio signal," *JAES* 6(2), 74–79**. L = M + delayed, R = M − delayed; the two transfer functions sum to unity, so **the mono sum is flat** while each channel is combed. Review: https://econtact.ca/8_3/gauthier.html
- **Decorrelation without coloration:** Kendall, G. S. (1995), "The Decorrelation of Audio Signals and Its Impact on Spatial Imagery," *Computer Music Journal* 19(4), 71–87 — decorrelators should break phase relationships while leaving the magnitude response unaltered. Later: allpass-cascade and velvet-noise decorrelators (DAFx-16, https://dafx16.vutbr.cz/dafxpapers/32-DAFx-16_paper_30-PN.pdf).
- **Perceptual reason to decorrelate anyway:** Zurek (1979) — a **diotic** reflection colours ~10 dB more audibly than a dichotic one below 5–10 ms (§6.3). Decorrelating ER taps buys coloration headroom in stereo at the cost of mono robustness. That trade-off has no published resolution.

---

## 10. ER time spans by room size, and classic digital structures

### 10.1 How long "early" lasts — citable anchors only

1. **Control/reference rooms: 15 ms.** EBU Tech 3276 (1998) — https://tech.ebu.ch/docs/tech/tech3276.pdf **[primary verified]**: "Early reflections are defined as reflections … which reach the listening area within the first **15 ms** after the arrival of the direct sound. The levels of these reflections should be **at least 10 dB below** the level of the direct sound for all frequencies in the range **1 kHz to 8 kHz**." Reverberation = "time delays more than about 15 ms." Room **volume should not exceed 300 m³**; nominal RT **0.2 < Tm < 0.4 s** with `Tm = 0.25·(V/100)^(1/3)` s. It names the mixing-desk top surface as a canonical strong early reflection.
2. **Speech/music boundaries: 50 ms and 80 ms.** ISO 3382-1 C50/D50 and C80 — the only **standardised** early/late splits.
3. **Halls: ITDG plus numerous, uniformly spaced, similar-strength reflections between the ITDG and 80 ms** (Beranek's criterion for the highest-rated halls).
4. **Physics-derived: Polack mixing time √V ms** — 10 ms (100 m³), 32 ms (1,000 m³), 141 ms (20,000 m³).

⚠️ **"Small room ER within ~5–30 ms, hall 20–100 ms" is folklore from effects-unit manuals and forums.** No published source states it. It brackets the anchors above reasonably; present it as "consistent with," not "as reported by." The honest derivation for a small room is arithmetic: a listener 1 m from a wall gets the first reflection at ~2 m excess path = **5.8 ms**; 3 m from a wall = **17.5 ms**.

### 10.2 Classic structures, ER-focused

**Schroeder 1962** — 4 parallel combs → 2 series allpasses, or 5 series allpasses. Comb delays **30–45 ms** (the 1:1.5 ratio), mutually prime. Allpass chain `M_i·T ≈ 100 ms / 3^i` → **100, 33.3, 11.1, 3.70, 1.23 ms**; with g = 0.708 the T60 is 2 s. Five series allpasses give **~810 echoes/s**; each stage roughly triples echo count. Loop gain ≤ 0.85. **There is no ER stage at all** — the ER/late split is a later invention. ⚠️ The delay values widely quoted as "the Schroeder reverb" (1687, 1601, 2053, 2251 samples; allpasses 347, 113, 37) are from a later CCRMA implementation tuned by ear by John Chowning at f_s = 25 kHz. Attribute them to Chowning/CCRMA.

**Moorer 1979**, "About This Reverberation Business," *Computer Music Journal* 3(2), 13–28 — the **first real ER structure**. Table 3 is a **19-tap delay line derived from a geometric simulation of Boston Symphony Hall** (tap 1 is the direct sound at t = 0, g = 1.0; 18 reflections follow). Transcription via an open-source reference implementation (https://github.com/LucaSpanedda/Digital_Reverberation_in_Faust):

| t (s) | g | t (s) | g | t (s) | g |
|---|---|---|---|---|---|
| 0.0043 | 0.841 | 0.0298 | 0.346 | 0.0612 | 0.181 |
| 0.0215 | 0.504 | 0.0458 | 0.289 | 0.0707 | 0.180 |
| 0.0225 | 0.491 | 0.0485 | 0.272 | 0.0708 | 0.181 |
| 0.0268 | 0.379 | 0.0572 | 0.192 | 0.0726 | 0.176 |
| 0.0270 | 0.380 | 0.0587 | 0.193 | 0.0741 | 0.142 |
| | | 0.0595 | 0.217 | 0.0753 | 0.167 |
| | | | | 0.0797 | 0.134 |

**ER span 4.3 → 79.7 ms** — the upper bound tracks the ISO C80 boundary closely. Late section: **6 parallel combs at 40, 41, 43, 55, 59, 61 ms**, feedback ~0.95, each with a **first-order lowpass in the feedback loop** (Moorer's key innovation over Schroeder, modelling air absorption — see his 600 m / 60 dB example in §7), then **one allpass**. ⚠️ Moorer's own comb-lowpass cutoffs are not reliably reproduced — implementers state they invented their own (https://christianfloisand.wordpress.com/2012/10/18/algorithmic-reverbs-the-moorer-design/). The post-comb allpass is quoted as either **6 ms @ g ≈ 0.7** or **0.007 s @ −0.09683 with 0.0017 s**; sources disagree.

**Gardner**, "Reverberation Algorithms," in Kahrs & Brandenburg (eds.), *Applications of DSP to Audio and Acoustics*, Kluwer 1998, 85–131 — https://link.springer.com/chapter/10.1007/0-306-47042-X_3. **Nested allpasses, more nesting for larger rooms**; small = double-nested → single-nested; medium = double-nested → allpass → single-nested; large = two series allpasses → single-nested → double-nested. Delay tables (⚠️ **transcribed from an open-source port of Gardner's block diagrams**, https://www.eumus.edu.uy/eme/ensenanza/electivas/csound/materiales/book_chapters/24mikelson/24mikelson.html, not a table Gardner printed):

| | Small | Medium | Large |
|---|---|---|---|
| Input LP | 6 kHz | 6 kHz | 4 kHz |
| Feedback BP | 1600 Hz / BW 800 | 1000 / 500 | 1000 / 500 |
| Delays (ms) | 24; 4.7, 22, 8.3; 36, 30 | 4.7, 8.3, 22; 5; 30; 67, 15; 29.2, 9.8; 108 | 8, 12; 4, 17, 31, 3; 25, 62; 120, 76, 30 |
| Coefficients | 0.15, 0.25, 0.30, 0.08, 0.3 | 0.25, 0.35, 0.45, 0.45, 0.25, 0.35 | 0.3, 0.3, 0.5, 0.25, 0.25, 0.5 |

Longest delay scales **36 → 108 → 120 ms**. Gardner has **no separate ER FIR** — diffusion and early response are the same allpass network. Free companion: Gardner (1992), "The Virtual Acoustic Room," MS thesis, MIT — https://www.ee.columbia.edu/~dpwe/papers/Gardner92-virtroom.pdf

**Dattorro 1997** — reference **f_s = 29,761 Hz**; rescale by f_s/29761. Input diffusion: 4 series allpasses at **142, 107, 379, 277 samples** (4.77, 3.60, 12.73, 9.31 ms), gains **0.75, 0.75, 0.625, 0.625**. Tank (figure-of-eight): **672 (or 762 — implementations disagree), 908, 4453, 4217, 1800, 2656, 3720, 3163 samples** (22.6 / 30.5 / 149.6 / 141.7 / 60.5 / 89.2 / 125.0 / 106.3 ms); decay diffusion **0.70 / 0.50**; damping 0.0005; 7 output taps per channel (266, 2974, 1913, 1996, 1990, 187, 1066 and 353, 3627, 1228, 2673, 2111, 335, 121), output scale 0.6. Cross-confirmed at https://github.com/jpcima/fverb and https://github.com/grame-cncm/faustlibraries/blob/master/reverbs.lib. **No ER stage by design — it is a plate model.** The ~30 ms of input allpass *is* the early response. A design arguing for a separate ER stage must address this counter-example.

**Feedback delay networks / Jot.** Stautner & Puckette (1982), *CMJ* — original 4-channel network, feedback matrix a sign-modified 4×4 Hadamard permutation. Jot & Chaigne (1991), AES 90th, preprint 3030 — https://aes2.org/publications/elibrary-page/?id=5663 — independent T60 per band plus a **tonal correction filter E(z)**. Householder `A_N = I_N − (2/N)u u^T` (multiply-free for power-of-2 N; at N = 4 all entries ±½); Hadamard multiply-free in fixed point for N a power of 4. Typical orders **4, 8, 16**. The standard Jot architecture puts **early reflections in a tapped delay line feeding the network**, with the direct signal scaled and summed at the output — the ER/late split is explicit. Reference: https://www.dsprelated.com/freebooks/pasp/FDN_Reverberation.html

⚠️ **Current dissent:** Dal Santo et al. (2024), arXiv:2404.00082 — "A typical approach is to use a delay network to only model the late reverberation while handling early reflections separately… Instead, we optimize the FDN such that it accounts for both early and late reverberation at the same time."

Griesinger's own design paper, "Practical Processors and Programs for Digital Reverberation," AES 7th Int. Conf. (1989) — https://aes2.org/publications/elibrary-page/?id=5469 — is paywalled and was not read.

### 10.3 ER tap-gain roll-off — what is published

**Nothing publishes a closed-form law.** Schroeder has no ER taps. Gardner and Dattorro generate early response implicitly through allpass diffusion. Moorer's taps come out of a geometric image-source simulation, so they encode 1/r spreading, per-surface absorption and geometry together.

Derivable from image-source theory: pressure ∝ **1/r = 1/(c·t_total)** per image, times the product of wall reflection coefficients — **roughly exponential in the number of reflections, hence roughly exponential in t**. The physically correct model is **1/t spreading × exponential absorption**, not one or the other. A check against Moorer's table (arithmetic, not a citation): gain ratio 0.841/0.134 = 6.28 across excess paths 1.47 → 27.3 m implies a direct distance of ~3.4 m under pure 1/r, which is plausible.

Survey for the whole lineage: Välimäki, Parker, Savioja, Smith & Abel (2012), "Fifty Years of Artificial Reverberation," *IEEE TASLP* 20(5), 1421–1448, DOI 10.1109/TASL.2012.2189567. ⚠️ Paywalled; nothing above is attributed to it.

---

## 11. Consolidated folklore flags

| Claim | Verdict |
|---|---|
| "A reflection can be 10 dB louder and you won't hear it" | **Misread of Haas.** 10 dB is the equal-*loudness* balance at 5–30 ms; it is heard as loudness, body and spatial change |
| "Haas fusion interval is 30–40 ms" | That is **Wallach et al. 1949** for complex sounds, and only at equal level (Toole) |
| "Haas spoke at 5.3 syllables/second" | **True** — verified in the paper; he also tested 3.5 and 7.4 |
| Blauert's 10/50/80 ms echo-threshold ladder; ±3.6° localization blur | **Standard but unverified** — book not opened |
| "Short ITDG = intimacy" | **Beranek withdrew this in 2004.** ITDG is not an ISO parameter and ranks last of his five predictors |
| Musikverein ITDG ≈ 14 ms | **Wrong** — Beranek tabulates 28 ms |
| "Best ITDG 12–25 ms"; "< 20 ms intimate, > 35 ms detached" | **Unattributed** rules of thumb |
| "Schroeder: 10,000 echoes/s" | **Wrong** — Schroeder said **1,000/s**; 10,000 is Griesinger via Jot & Chaigne 1991 |
| "Prime-number delays are required" | Published requirement is **mutually prime / incommensurate**; Blesser says the classic hardware values had "no mathematical basis" |
| "Reflections below −20 dB are inaudible" | **Contradicted** — Brunner et al. detected at −21.5 dB (piano) and −27 dB (snare) for individuals |
| "Diffusion hides reflections" | **Contradicted** — Robinson et al. 2013: diffuse reflections detected at *lower* levels than specular of equal energy |
| "Bilsen's coloration threshold is −20 dB" | **Unsourced.** Only the shape (minimum at 2–5 ms) is verifiable |
| "Auditory horizon ≈ 15 m" | **Unsourced** — Kolarik et al. 2016: "has not yet been measured directly" |
| Distance exponent "a ≈ 0.4–0.8" | Published: **mean 0.54, none above 0.9** (84 data sets) |
| "C50 > +2 dB", genre C80 tables, "D50 > 0.6/0.7" | **Vendor-blog folklore.** Sourced: C80 −2…+2 dB (Barron); C50 > 0 dB ⇔ D50 > 0.5 |
| "C80 −5 to +5 dB is the target" | That is ISO's **typical measured range** |
| "LF 0.20–0.30 in good halls"; "BQI ≈ 0.66 Boston/Vienna" | **Unsourced** |
| "Small room ER 5–30 ms, hall 20–100 ms" | **Folklore.** Use EBU 15 ms, ISO 80 ms, Polack √V |
| "Schroeder reverb delays 1687/1601/2053/2251" | **Chowning's CCRMA implementation**, f_s = 25 kHz |
| ISO 3382-1 IACC JND 0.075 | **Cox, Davies & Lam 1993**, not ISO. ISO lists **L_J JND as "not known"** |
| IACC_L upper integration limit | **Inconsistent across sources** — state the ambiguity |
| ISO 9613-1 Table 1 at 20 °C quoted second-hand | **Unobtainable free; do not trust it.** Use ISO 9613-2 Table 2 octave bands, noting they are not pure-tone values |
| Moorer comb lowpass cutoffs; post-comb allpass values | **Unverified / sources disagree** |
| Gardner delay tables | **Transcription of block diagrams**, not a printed table |
| Lindau 2012 mixing-time regression constants | **Not extracted** — only R² confirmed |

---

## 12. Sources to buy or borrow before publishing

- ISO 3382-1:2009 — Annex A Table A.1, the 5 ms rationale, the IACC_L limit
- ISO 9613-1:1993 — Table 1 pure-tone coefficients at 20 °C
- Beranek, *Concert Halls and Opera Houses* (2004) — per-hall ITDG and BQI
- Blauert, *Spatial Hearing* (1997) — echo-threshold ladder and localization blur table
- Bilsen, *Acustica* 19, 27–32 — coloration thresholds in dB
- Lindau, Kosanke & Weinzierl, *JAES* 60(11) — mixing-time regression constants
- Moorer, *CMJ* 3(2) — Table 3 and the comb lowpass values
- Bradley & Soulodre, *JASA* 98(5) — LEV correlations
- Olive & Toole, *JAES* 37(7/8) — read the threshold curves off the actual figures
- Kuttruff, *Room Acoustics* — the echo-density equation number and the speech reflection threshold
- Välimäki et al., *IEEE TASLP* 20(5) — the survey
- Griesinger, AES 7th Int. Conf. (1989) — his reverberator design paper

---

## 13. Design reads

Conclusions already present in the sections above, collected. None of these has been measured or heard by us.

- **Echo disturbance vs level (Haas):** at equal level the 50 % disturbance point for speech is 68 ms; −3 dB moves it to 108 ms, −6 dB to 175 ms, and a tap more than **10 dB below the direct never disturbs**. 5 dB of attenuation roughly doubles the usable delay. (§1.2)
- **Roll off HF on taps.** Haas found HF-rich echoes disproportionately disturbing and that attenuating their highs raises the critical delay at almost no loudness cost; Jot & Chaigne give the decay-rate argument; Moorer gives the air-absorption argument. (§1.2, §6.4, §7)
- **The +10 dB Haas figure is an equal-loudness balance at 5–30 ms**, not inaudibility. Strong early taps are heard as level, body and width. (§1.2)
- **Single-reflection map:** image shift below ~5–8 ms, colouration ~10–50 ms, ASW 0–45 ms, echo from ~50 ms; all strongly signal-dependent. (§1.3)
- **Box-colour window is 5–20 ms** (comb spacing 50–200 Hz); coloration thresholds are lowest at 2–5 ms; comb ripple is detectable with a tap more than 20 dB down. A −6 dB tap gives 9.6 dB peak-to-notch. (§6.1–6.3)
- **Decorrelate ER taps L/R for ~10 dB of coloration headroom** (Zurek: diotic echoes colour ~10 dB more audibly than dichotic below 5–10 ms) — **against mono robustness**, because a mono sum removes exactly the inter-channel difference that creates width. The **complementary-comb construction (Lauridsen/Schroeder 1958) is the mono-flat option.** No published study resolves this trade-off. (§6.3, §9.3)
- **Do not assume diffusion hides a tap.** Temporally diffused reflections were detected at lower levels than specular ones of equal energy. (§6.4)
- **Griesinger's caution:** excess reflected energy in **10–100 ms** reduces engagement whether lateral or not; reflections **within ~5 ms help** proximity and **after ~7 ms hurt** it; keep roughly the first 20 ms clear of strong energy, keep the summed reflections in the first 100 ms about 3 dB under the direct; 50–120/150 ms is the worst place for energy; envelopment comes from energy at **150–160 ms and later**. This sits **against the lateral-reflection orthodoxy** (Barron & Marshall; Pätynen et al. 2014: lateral energy gains 1–5 dB binaurally above 2 kHz and enhances dynamics). Both positions are published; neither is settled. (§3, §4)
- **Early lateral energy gives width, late lateral energy (≥ 80 ms, Griesinger argues ≥ 160 ms) gives envelopment.** In a small-room time scale only the source-bound "small room" impression is available. (§3, §4)
- **Spatial measures weight 125–1000 Hz; clarity measures weight 500–2000 Hz.** Width-building taps belong in the low-mid band, not broadband. (§3)
- **C50 and D50 are one quantity** (D50 = 0.5 ⇔ C50 = 0 dB). The best halls sit at C80 ≈ −2…−0.7 dB; higher clarity is not better for music. Early reflections inside ~80 ms raise effective speech SNR. (§5)
- **Cue room size mainly with RT, level and clarity**; use the ER pattern as corroboration. ER alone supports ~3 % mean-free-path discrimination; the full response ~1 %. Shape-from-ER perception is unproven. (§9.2)
- **ITDG is pre-delay, not an intimacy control.** Good halls 15–28 ms; above ~40 ms reads as remote. (§2)
- **The ER section should run to about the mixing time, √V ms** (10 ms at 100 m³, 32 ms at 1,000 m³, ~140 ms at 20,000 m³), or to where t² echo density reaches ~1,000/s, before the statistical tail takes over. Listeners notice echo-density character changes at NED 0.3 and 0.7. (§8)
- **Tap density:** ~1,000 echoes/s for flutter-free (Schroeder), up to 10,000/s for impulsive sources (Griesinger); **velvet-style sparse sequences sound smooth at ≥ 2,000 pulses/s, and at ~600/s if low-passed near 1.5 kHz**; jittered-grid placement beats fully random. (§6.4, §8)
- **Delay choice:** incommensurate / mutually prime, not literally prime; 3–4 parallel combs minimum for spectral density; total recirculating delay ≥ 0.15·T60·f_s samples. (§6.4, §8)
- **Tap-gain law:** 1/t spreading × exponential absorption; Moorer's measured-hall taps fit 1/r from a ~3.4 m source. (§10.3)
- **Distance:** DRR's 6 ms direct window means **early reflections count as reverberant energy** for distance; DRR JND is 2–8 dB, intensity ~1 dB and weighted more heavily; perceived distance compresses with exponent ~0.54. Air absorption matters on the cumulative tail path, not the direct path. (§7)
- **Small-room modal region** runs to ~110–150 Hz; a delay network's own modes will be audible there. (§8)
- **Reference-room anchor:** reflections inside the first 15 ms at least 10 dB down from 1–8 kHz (EBU). (§10.1)
- **Separate ER stage is a choice, not a given:** Moorer and Jot use one; Schroeder, Gardner and Dattorro do not; recent delay-network work argues for folding ER into the network. (§10.2)
