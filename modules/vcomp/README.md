# LTV Comp

A modern vocal compressor. Two knobs and a gate do the work; five more controls
are behind a switch for when they are wanted.

## The face

| Control | What it does |
|---|---|
| **AMOUNT** | How hard it works. Sweeps threshold, knee and ratio together (1:1 up to 8:1) and adds its own makeup, so the vocal gets denser and more forward rather than louder. At 0 the module is a wire. |
| **GATE** | The handle on the IN meter. Drag it to the level below which the track should be shut down -- room tone, bleed, breaths. It is there rather than on a knob because a gate threshold is set by watching the level you are setting it against. At the far left it is off. |
| **MAKEUP** | Level, by ear, on top of that automatic makeup. The id is `output`; see params.h. |
| **COMPLEX** | Reveals the five below and makes the DSP read them. Off, they are ignored entirely and the module runs 5 ms attack, 200 ms release, ARC on, 90 Hz sidechain, no band split. |
| **ARC** | Programme-dependent release: quick recovery after a consonant, slow after a sustained phrase. Always on in standard mode, which is why the switch is lit and locked there. |

## Behind COMPLEX

| Control | What it does |
|---|---|
| **ATTACK** | 0.1 to 100 ms. |
| **RELEASE** | 20 ms to 1 s. Still in charge with ARC on -- it scales the whole behaviour rather than being switched out. |
| **SC HPF** | A high-pass on what the compressor **listens to**, never on the audio. Keeps plosives and proximity effect from ducking the phrase. 20 Hz is the bottom of the range and is flat over anything a voice does. |
| **LOW THRU** | Everything below this passes through **uncompressed**. The chest of a voice keeps its weight while the midrange is levelled. At 20 Hz it is off. |
| **HIGH THRU** | Everything above this passes through uncompressed. Air and sibilance keep their top over a held-down body. At 20 kHz it is off. |

**SC HPF and LOW THRU are not the same control.** SC HPF changes what the
compressor hears; the low end still gets ducked along with everything else, it
just stops being what triggers the ducking. LOW THRU changes what the
compressor acts on: that band is genuinely not reduced.

## The meters

Three bars, top to bottom: **IN**, **GR**, **OUT**. IN and OUT read left to
right in dBFS over 60 dB; GR reads right to left from zero, up to 24 dB, the way
a gain-reduction meter has always read -- the bar shows what is being taken
away. Ticks are every 12 dB.

The gate handle lives on the IN bar, on the same scale the level is drawn on,
so where you put it is the level it acts at.

## The rest

**Zero latency at every setting.** No lookahead, no oversampling, so it can sit
on a vocal while the singer is listening to it. The band split is IIR, so it
costs phase rather than samples, and the limiter is instantaneous rather than
looking ahead, for the same reason.

**A limiter catches what the makeup would have clipped.** Last in the chain,
no controls, ceiling a hair under full scale -- so it is inert for anything
that was not going to clip anyway, and the module never puts out an over.

**Stereo is always linked**, with no switch: two channels of one voice
compressed independently is a wandering image, not an option. The gate is
linked the same way, so one channel never opens without the other.

## Presets

| Preset | |
|---|---|
| Init | AMOUNT 0 -- a wire |
| Lift | levelling, barely a sound of its own |
| Forward | the vocal sits up; the working setting |
| In Front | dense and modern, consonants held down |
| Fast Vocal | complex: catches consonants and lets go quickly |
| Smooth Lead | complex: lets the transient through, rides the body |
| Keep The Chest | complex: body levelled, low end left alone |
| Keep The Air | complex: top stays open over a held-down body |
| Manual | complex, ARC off: the release is the number on the knob |

None of them set OUTPUT, and none set GATE. AMOUNT carries its own makeup, so a
preset that moves AMOUNT alone is level-matched by construction and cannot
drift when the curve is revoiced -- which is why this is the one module in the
suite with no hand-solved makeup figures in its preset file. The gate is left
off because a threshold is an absolute level: the right one depends on the
track, and a number chosen here would be wrong for almost every session it
loaded into.

**Heard on real material 2026-09-14 and kept.** `tools/measure/vcomp` prints the tables and
writes the WAVs for that pass:

```bash
measure_vcomp curve
```

```bash
measure_vcomp presets --outdir /tmp/vcomp
```

See `AGENTS.md` for what is still open and what was deliberately left out.
