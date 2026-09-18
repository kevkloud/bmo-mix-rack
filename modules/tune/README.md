# BMO Tune RT -- the DSP

What the plugin does to a voice, in plain terms. `AGENTS.md` beside this file
has the engineering detail and the evidence for every number here.

## What it listens for

The detector finds the pitch of a single voice from about 80 Hz to 1.4 kHz
(the Auto range; Bass and Instrument reach down to 55 Hz). It locks onto a
new note in about two and a half cycles of that note -- 5 ms on an A4, 11 ms
on an A3 -- and tracks it to a small fraction of a cent. Breaths, consonants
and silence are left alone.

## What it does about it

It pulls the voice to the nearest allowed note -- chromatic by default, or a
key with a major or minor scale, with any note switched out -- as fast as
Retune Speed says. Retune Speed is in milliseconds, as the other tuners print
it: 0.0 to 5.0 ms in tenths, where hard tuning lives, then 6 to 100 ms in
whole ones. At 0.0 ms, the default, the snap is immediate: that is the sound
this plugin is built for. (0.1's Retune was a unitless 0-100 knob; its 10
was about 1.2 ms. A 0.1 session reopens at 0.0 ms.)

- **Vibrato** 0 % flattens the singer's vibrato onto the note; 100 % keeps it
  and only corrects the note it is centred on; 150 % exaggerates it.
- **Relax** leaves small deviations alone. Off by default. (Parameter id `flex`.)

## How it moves the pitch

It reads the voice faster or slower, repeating or dropping whole cycles to
keep up. Formants move with the pitch, which is the bright, slightly
synthetic quality the classic hard-tune sound has.

There was a second engine, Hybrid, which kept the formants where they were.
Classic sounded better in Ableton (2026-09-11), so it is the only one; Hybrid
is kept for a possible non-real-time tuner --
`testing-notes/nrt-tune-handoff-2026-09-11.md`.

## Latency

It reports zero to the host and costs 4.0 ms while it is not correcting.
While it corrects, it runs up to one cycle of the note later still. That is
the only mode: it is what a singer monitoring through the plugin needs.

Measured the same way on the same test file, its worst is 9.2 ms, against
Antares Auto-Tune Artist's 10.7 ms and Waves Tune Real-Time's 19.2 ms --
and neither of those tells the host what it really costs either. The rule for
changes: BMO may never be later than Waves (the root `AGENTS.md`, "The
latency rule").

**Read note by note, that flatters it.** Those three figures are all taken at
the lowest note in the test, which is where a period-proportional delay costs
the most and BMO's flat 4 ms costs the least. The other two tuners' delays
track the note; BMO's does not. So BMO is the least late of the three on a
bass note and the latest of the three on a high one -- 4.6 ms at A5 where
Waves is 0.7. It is over Waves from about C3 upward, which is most of the
range, and `tests/dsp/tune/HardTuneTests.cpp` fails on it today. See
`testing-notes/tune-latency-review-2026-09-11.md`.

## CPU

About 0.9 % of one core at 48 kHz with 128-sample buffers, on AURORA, the
laptop this was built on.
