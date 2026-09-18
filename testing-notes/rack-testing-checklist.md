# BMO Mix Rack — Ableton testing checklist

Build under test: `v0.2.4` / `cbd0939` on `integration`, CI run
**34834557823** (`BMO-Windows`), installed on **AURORA** 2026-09-14; SHA-256
in `review-0.2.4-2026-09-14.md`. The rack hosts seven modules (Util, EQ,
Sat, Opto, Dimension, DEQ, Vcomp, which is the add-menu order); BMO Tune RT
is not in it. Drafted from a code review.

There is no per-slot bypass and no module bypass: EQ's EQL and Sat's SAT
switch a circuit out, not the module. Bypass is Live's device switch.

## 1. Chain editing while playing
- Add a module to each of the 8 slots; remove from the middle; move a
  slot left and right past both ends; clear the chain. No crash, no stuck
  audio. Expect a few blocks of unprocessed audio during an edit (the
  audio thread passes through while the chain lock is held); note if it
  is more than a blip.
- Remove a slot while its panel is open and a knob is being dragged.

## 2. Latency compensation
- Init rack: 0 samples. Add EQ (default 2x): 40. Two EQs: 80. Set one to
  8x: 110. Change EQ's Oversampling from the slot lane while playing:
  PDC re-syncs (with a click from the module itself). Remove the EQ:
  back to 0. Null-test against a dry duplicate at each step.
- Sat at Off adds nothing; Ruined (4x) adds 60.

## 3. Chain presets
- Each chain preset builds the right modules in the right order with the
  stated values; loading one after another leaves nothing behind.
- Anything clearly louder than Init is a fault.

## 4. Session save and reload
- Build a chain with values on every slot, an expanded DEQ, and an EQ at
  8x; save the Live set; reopen. Same modules, same order, same values,
  same DEQ view, same reported latency.
- DEQ has 158 parameters and only 32 host lanes: set a band past lane 32
  from the panel, save, reopen. It must survive.

## 5. Automation of slot lanes
- Automate `slot1_p01` on an EQ, then move the EQ to slot 2: the lane
  stays with slot 1 (lanes are per slot, not per module). Is that the
  expected behaviour or a surprise?
- Automate a stepped lane (EQ Mid Freq): stepped in the lane, glides in
  the audio. Automate a switch lane (Util Phase L): clicks (known).
- DEQ in a slot: only its first 32 parameters have lanes; confirm the
  33rd and up are absent from Live's lane list and still editable on the
  panel.
- Swap the module in a slot while its lane is showing in Live: does the
  lane's name refresh?

## 6. The DEQ expand button in a slot
- DEQ opens compact in a rack; the button on the slot bar expands it.
  Neighbouring panels reflow; nothing overlaps; the view is remembered
  across save and reload and follows the module when it is moved.
- Loading a rack preset rebuilds the chain compact by design; note it.

## 7. CPU with 8 slots full
- 8 × EQ at 2x, stereo, 48 k / 128: Live's CPU meter. Then 8 × EQ at 8x.
  Then a realistic chain (Util, EQ, Sat, Vcomp, DEQ, Dimension, Opto,
  Util). Record the three figures and the machine.
- Any dropout while editing the chain at the 8x setting.

## 8. Look
- Rack with one of every module: input knobs, switch rows and output
  knobs should line up across columns. Today Vcomp's OUTPUT and Opto's
  MAKEUP sit on their own lines. Opto's greyscale, Dimension's lavender and
  Util's placeholder purple should read as three different things; today
  the last two do not.

---
Feed back whatever you notice, however informally. Name the machine.
