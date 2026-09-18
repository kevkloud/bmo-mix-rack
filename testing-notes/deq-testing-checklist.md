# BMO DEQ — Ableton testing checklist

Build under test: `add-bmo-deq` on the fork (badmixesonly/bmo-mix-rack-333),
built by the fork's Actions run. What the tests can settle is settled, on
AURORA: 14/14 ctest in Release and 6/6 DSP-only, zero latency with every band
dynamic, the schema and the rack lanes frozen, both panel widths laid out with
every caption, value and switch label measured, and the DSP held to its spec
(`modules/deq/AGENTS.md`). What is left is what only a host and ears can
answer.

Run `deq-topology-listening.md` first if it has not been: it decides whether
the bands stay in series, and everything below assumes they do.

## 0. Install

**From CI (preferred):** the newest green `build` run on `add-bmo-deq` in the
fork's Actions tab, artifact **BMO-Windows**. With Live closed, copy
everything in its `VST3/` folder into `C:\Program Files\Common Files\VST3\`
(admin), replacing what is there, then rescan. Install it last: a later
non-Release local build copies itself over the same folder. The rack bundle
has to come from the same package: an older BMO Mix Rack does not know BMO
DEQ exists.

**From a local build:** a local Release build does not install itself
(`CMakeLists.txt`: only non-Release builds copy plugins, and those overwrite a
tester install). Copy by hand, with Live closed:

    build-full/products/deq/BmoDeq_artefacts/Release/VST3/BMO DEQ.vst3
    build-full/products/rack/BmoMixRack_artefacts/Release/VST3/BMO Mix Rack.vst3

into `C:\Program Files\Common Files\VST3\` (admin), then rescan. The rack
bundle has to come from the same build: the one already installed does not
know BMO DEQ exists.

## 1. The two widths

- [ ] BMO DEQ on a track opens **full** (600). The header's chevron switch
      collapses it to **compact** (320) and back; the window keeps its scale.
- [ ] In BMO Mix Rack it arrives **compact**. The slot bar's switch widens
      it, the modules to its right move over, and the rack window grows.
- [ ] Save the Live set with one of each switched the other way, close, reopen:
      each comes back the way it was left.
- [ ] Load a preset: the width does not change.

## 2. The curve

- [ ] Drag a node: frequency and gain follow, and Live's automation lane for
      that band (1-6) moves. Wheel over the curve: the selected band's Q.
- [ ] Double-click an empty spot: the next free band switches on there.
- [ ] The line matches what you hear. Two bands at one frequency add (+6 and
      +6 is +12); a low cut under a low-shelf boost still removes the rumble.

## 3. Dynamics

- [ ] A dynamic band on a vocal's sibilance (7 kHz, threshold low, range
      −6 to −10): the GR bar moves on the esses and rests between them.
- [ ] Switch it to BELOW with a positive range: it lifts the band only in
      the quiet parts.
- [ ] Fast attack (0.1-1 ms): the first transient still gets through before
      the band catches it. That is expected -- zero latency means no lookahead,
      and the spec budgets it rather than hiding it. Note how it sounds.

- [ ] The value under each knob follows it, and follows Live's automation
      too. Compact shows the short form ("2.10k"); full shows units.

## 3b. AUTO and shelf Q

- [ ] A wide +6 dB bell with AUTO on: the track's loudness barely moves as
      the band comes in. Switching AUTO glides, it does not click.
- [ ] A de-esser (dynamic band, negative range) with AUTO on sounds the same
      as with it off -- AUTO answers to the static curve only.
- [ ] Set a band to a shelf and turn Q past 2: on release the knob comes back
      to 2.00. A shelf at Q 2 does overshoot: about +6 dB past the corner and
      a −6 dB dip before it on ±24 (±4.6 on ±12, measured 2026-09-14). That is
      the RBJ shelf, not a fault; whether the cap should be nearer 0.7 is the
      question to answer here. A bell still goes
      to 40.

## 4. Zero latency, in the host

- [ ] Live's delay compensation readout for the track shows 0 with every band
      on and dynamic, at 44.1, 48 and 96 kHz.
- [ ] Null test: DEQ with every band off against the dry track nulls
      completely.

## 5. Automation

- [ ] In the rack: bands 1-6's frequency, gain, Q, threshold and range, output,
      and DEQ are automatable; nothing else is (by design -- the rest are
      standalone-only).
- [ ] Standalone: every one of the 159 is.
- [ ] Automate a band's frequency across its whole range quickly: no clicks.
