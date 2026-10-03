# BMO Linger — the M3a listening set (the tail)

**Rendered on ICE QUEEN, 2026-10-02**, from `frosty-linger-m3a` at
`ef63a73` plus the render fixes below. The set is in
`D:\VISUAL\PLUGINS\MIX RACK\LINGER\set-2026-10-02-m3a\` on ICE QUEEN,
outside the repository: 43 files, 24-bit, at each source's own 44.1 kHz.
**Nothing in it has been heard yet.** M3b waits on this pass.

Every file is the engine that ships, rendered by `measure_reverb render`.
**Every file is at OUTPUT −4 dB, the dry references included**, because at
MIX 50 the dry stays at unity with the reverb on top, and the hot vocal went
over full scale. Loudest file: −1.8 dBFS. A/B a render against its `0-dry-`
file, never against the original clip, which is 4 dB louder.

`type=` now selects a type the way the panel does: it loads that type's
voicing (SIZE, DENSITY, SPREAD, SOURCE, levels, DARKEN, modulation), so
"Hall" means Hall. DECAY is not part of a voicing, so files that need one
name it. The tail is rendered in full past the clip's end, up to the reported
tail length.

**What the tail does not have yet, so do not judge it on these:**
- no modulation (M3b): a held note may sound static or slightly metallic, and
  the question is how much
- no Reverb EQ and no DARKEN in the path (M3b)
- no onset bloom, the per-type ATTACK (M3b)
- type constants are placeholders apart from Room (M4)

## The questions, and the files that answer them

0. **References.** `0-dry-*`: each source at the same −4 dB.
1. **Each type on a vocal, at its own voicing, MIX 50.** `1-vocal-room /
   chamber / hall / cavern / plate / ambience-mix50`. Does each read as what
   it is called? Is the tail the right size under the ER? Does anything cloud
   the vocal?
2. **The tail alone, on a snare, every type.** `2-snare-<type>-tail`: ER off,
   MIX 100. Listen for flutter (a fast, regular ripple in the decay),
   metallic ringing, and whether the decay is smooth. **Plate is the one the
   numbers flag**, by a hair.
3. **DECAY.** `3-snare-hall-decay0.5 / 1.8 / 4 / 10-tail`. Does each sound
   as long as it says? Does 0.5 s sound like a short room or a chopped tail?
4. **Damping.** `4-vocal-hall-high0.25 / 1 / 2-mix50` (the top end decays at
   that multiple of DECAY 2.5 s) and `4-vocal-hall-low0.5 / 2-mix50` (the
   bottom). Are 0.25 dark and 2 bright without anything odd in the middle?
5. **The handover from early reflections to tail: SOURCE.**
   `5-woodblock-hall-source0 / 70 / 100-wet` and the same for Room, ER and
   tail both on. 0 feeds the tail the dry, 100 feeds it the early reflections,
   and 70 is the default. Is there a gap or a second attack between the
   cluster and the tail? Which feels most like one room?
6. **PRE-DELAY.** `6-rap-hall-predelay0 / 40 / 120-mix50`. The tail moves
   back and the dry and ER should not. Does 40 ms clear the words?
7. **Plate on a vocal.** `7-vocal-plate-decay1.2 / 2.5-mix50` against
   `7-vocal-hall-decay2.5-mix50-compare`. A plate is meant to be dense,
   bright and immediate. Plate's numbers are the weakest of the six (modal
   density and envelope flutter, both by a hair), so this is where to hear
   whether that matters.
8. **A held note, before modulation.** `8-heldnote-hall / cavern /
   plate-decay4-mix50`, and `8-acoustic-chamber-mix50`. How static or
   metallic is the tail on a sustained pitch? This sets how much M3b's
   modulation has to do.

**Not in the set: a SIZE move under a held note.** The render tool has no
automation. The tests hold it to 0.34 dB of 1 ms energy jump. It is worth
hearing in a host once there is a build.

## Answers

*(Frosty's, by number, when heard. Say which headphones or monitors, and
whether the amp was in stereo.)*
