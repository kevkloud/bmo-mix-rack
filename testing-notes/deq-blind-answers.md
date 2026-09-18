# BMO DEQ — blind listening answers, serial vs parallel

Each case is a pair: `<case>-A.wav` and `<case>-B.wav`, one serial, one
parallel, **shuffled per case**, so preferring "A" twice means nothing.
**Don't open `key.txt` in any source folder until every line here has
something on it.** Nothing in the session has opened one.

**How to answer:** comment on the bold line under each case in the Files pane
— it has no save button, so nothing here is typed into directly. The session
writes your comments into the file, ticks the source off below, and opens the
key only when the last one is in. Same as Tune's rounds.

A word is enough. **"Same" is a real answer and a useful one**; on most
material most of these should be hard to tell apart, and the cases where they
are not are the interesting ones. If a pair is odd in a way the line doesn't
ask about, say that instead — a click, a level jump, anything that sounds like
a fault rather than a topology.

## Progress

The session ticks these off as your comments come in.

| | source | cases | done |
|---|---|---|---|
| 1 | vocal | 8 | done |
| 2 | 808 | 5 (7 and 8 skipped — no dynamics on this source) | done |
| 3 | drum loop | 8 | done |
| 4 | synth | 5 | done |
| 5 | guitars | 8 | done |
| 6 | room | 7 | done |
| 7 | full mix | 8 — ⚠ monitoring down 6 dB | done |
| | the verdict | | after the key, below |
| | key opened | | 2026-09-11, once all 51 were in |

**Each source starts with its control case.** The two renders differ by −32 to
−41 dB relative to input there, which should be inaudible. If you can hear a
difference in the control, something other than the topology is reaching your
ears — level, playback order, a click at a file start — and nothing else from
that source can be trusted until it is sorted out.

**Watch the level.** The full mix (source 7) renders **above 0 dBFS in every
case**, up to +5.0: drop the monitoring 6 dB before it. `stacked-boosts` also
hits −0.0 dBFS on the vocal and −4.4 on the drums. Same gain across A and B of
a pair, always.

**Seven sources, in the order they were rendered.** The first four came from
the first upload; the guitars, the room and the mix arrived after the first
GTR and ROOM exports turned out to be silent files.

Within each source the cases are ordered **loudest difference first**, and the
measured figure is given so you know what to expect. The ones marked "expected
inaudible" are there for completeness: one line each is plenty.

---

## Source 1 — Failure, dry vocal

Both dynamic cases engaged fully here (10.0 / 10.1 dB of gain reduction), so
this source and the drum loop are where cases 7 and 8 mean anything.

### 6. vocal-control — the validity check (−32.5 dB)

- **Could you tell them apart?** Should be no. If yes, say what gave it away -- and stop there, the setup is leaking:  no

### 2. stacked-cuts-12 — two −12 dB cuts at 1 kHz (−7.3 dB; peaks differ by 4.3 dB)

- **Same or different, which you prefer, what you heard:**  B sounds exponentially better. A sounds totally bell filtered or extreme hi cut

### 3. stacked-cuts-6 — two −6 dB cuts at 1 kHz (−15.6 dB)

- **Same or different, which you prefer, what you heard:**  same

### 4. stacked-boosts — two +6 dB boosts at 3 kHz (−17.0 dB) ⚠ peaks at −0.0 dBFS

- **Same or different, which you prefer, what you heard:**  slight bias toward b for no reason, the sound almost identical. pure tone around 2500 or 3k peaks through in some parts

### 1. lowcut-under-shelf — low cut 80 Hz under a +6 shelf at 100 (−26.7 dB)

- **Same or different, which you prefer, what you heard:**  same

### 8. tamer-dynamic — dynamic −12 at 2.5 k over a static +3 at 2 k (−29.8 dB, 10.1 dB GR)

- **Same or different, which you prefer, what you heard:**  A sounds better, tamer plosives

### 7. deess-dynamic — dynamic −10 at 7 k over a static +4 shelf at 8 k (−37.1 dB, 10.0 dB GR)

- **Same or different, which you prefer, what you heard:**  almost identical, slight preference for B

### 5. surgical — −12 at 2.5 k Q 8, −9 at 3.1 k Q 8 (−31.0 dB, expected inaudible)

- **Same or different:**  same

---

## Source 2 — PHRYGIAN D 808

The low-end case. Cases 7 and 8 never engaged on this material (0.9 and
0.0 dB of gain reduction), so they are two static filters here and say nothing
about dynamics — skip them unless curious.

### 6. vocal-control — the validity check (−40.6 dB)

- **Could you tell them apart?** Should be no. If yes, say what gave it away -- and stop there, the setup is leaking:  no

### 1. lowcut-under-shelf (+1.3 dB — the largest difference in the whole run; peaks differ by 4.1 dB)

This is the case the whole argument is about, on the source that shows it.

- **Same or different, which you prefer, what you heard:**  B preferred, bass is much fuller

### 2. stacked-cuts-12 (−29.7 dB)

- **Same or different, which you prefer, what you heard:**  different, b leaves the high end of the 808 in tact. not sure which is preferred without seeing where the cuts were. If cuts were to high end, A did its job, b did not. If the cuts were supposed to cancel, B remained most unchanged

### 4. stacked-boosts (−35.0 dB, expected inaudible)

- **Same or different:**  same

### 3. stacked-cuts-6 (−38.6 dB, expected inaudible)

- **Same or different:**  same

### 5. surgical (−48.5 dB, expected inaudible)

- **Same or different:**  same

---

## Source 3 — PHRYGIAN D drum loop

The most useful source in the set: every static case is live, and both dynamic
cases engaged on transients (7.5 / 8.7 dB), which is where a topology
difference would show as pumping or smearing rather than as tone.

### 6. vocal-control — the validity check (−33.9 dB)

- **Could you tell them apart?** Should be no. If yes, say what gave it away -- and stop there, the setup is leaking:  cant tell them apart

### 1. lowcut-under-shelf (−2.0 dB)

- **Same or different, which you prefer, what you heard:**  A has more low end

### 4. stacked-boosts (−10.4 dB) ⚠ peaks at −4.4 dBFS

- **Same or different, which you prefer, what you heard:**  A has a much more audible boost around 1k

### 2. stacked-cuts-12 (−12.8 dB; peaks differ by 2.0 dB)

- **Same or different, which you prefer, what you heard:**  A sounds like its been EQ'ed, B sounds unedited

### 3. stacked-cuts-6 (−21.2 dB)

- **Same or different, which you prefer, what you heard:**  A sounds like a lower low mid cut, B preserves more high end info

### 5. surgical (−23.7 dB)

- **Same or different, which you prefer, what you heard:**  cant differentiate

### 8. tamer-dynamic (−26.1 dB, 8.7 dB GR)

- **Same or different, which you prefer, what you heard:**  can't really differentiate, still some slight preference toward A

### 7. deess-dynamic (−33.0 dB, 7.5 dB GR)

- **Same or different, which you prefer, what you heard:**  same

---

## Source 4 — PHRYGIAN D synth

Mid-dense and sustained: where stacked curves that do not add should show as
tone. The dynamic cases barely engaged (0.9 / 1.9 dB) — skip unless curious.

### 6. vocal-control — the validity check (−32.6 dB)

- **Could you tell them apart?** Should be no. If yes, say what gave it away -- and stop there, the setup is leaking:  cant tell

### 1. lowcut-under-shelf (−6.8 dB)

- **Same or different, which you prefer, what you heard:**  different, A had more mid clarity less low, B had more low

### 2. stacked-cuts-12 (−10.3 dB)

- **Same or different, which you prefer, what you heard:**  A sounds EQ'ed, B sounds unedited

### 4. stacked-boosts (−12.0 dB)

- **Same or different, which you prefer, what you heard:**  A maintains high mid, B less high mid

### 3. stacked-cuts-6 (−18.5 dB)

- **Same or different, which you prefer, what you heard:**  A is audibly EQ'ed, B sounds unchanged.

### 5. surgical (−25.4 dB)

- **Same or different:**  A maintains initial attack better than B. Not sure what target frequency or curve was, no preference

---

## Source 5 — DEQ ref GTR

Guitars sit where the stacked boosts live, so case 4 is unusually live here.
Both dynamic cases engaged (5.9 / 7.2 dB).

### 6. vocal-control — the validity check (−32.3 dB)

- **Could you tell them apart?** Should be no. If yes, say what gave it away -- and stop there, the setup is leaking:  can't tell

### 1. lowcut-under-shelf (−7.6 dB)

- **Same or different, which you prefer, what you heard:**  same

### 4. stacked-boosts (−8.5 dB; peaks differ by 1.5 dB)

- **Same or different, which you prefer, what you heard:**  A noticeably louder/harsher

### 2. stacked-cuts-12 (−13.2 dB)

- **Same or different, which you prefer, what you heard:**  A has significantly less high end, B louder

### 3. stacked-cuts-6 (−21.8 dB)

- **Same or different, which you prefer, what you heard:**  different, b preferred

### 5. surgical (−22.1 dB)

- **Same or different, which you prefer, what you heard:**  same

### 8. tamer-dynamic (−28.2 dB, 7.2 dB GR)

- **Same or different, which you prefer, what you heard:**  same

### 7. deess-dynamic (−33.8 dB, 5.9 dB GR)

- **Same or different, which you prefer, what you heard:**  same

---

## Source 6 — DEQ ref ROOM

The one source where a difference could show as **space** rather than tone —
decay tails and early reflections rather than a steady spectrum. Listen past
the front of each sound. The dynamic cases only half-engaged (3.8 / 4.7 dB).

### 6. vocal-control — the validity check (−31.4 dB)

- **Could you tell them apart?** Should be no. If yes, say what gave it away -- and stop there, the setup is leaking:  no difference

### 1. lowcut-under-shelf (−10.2 dB)

- **Same or different, which you prefer, what you heard:**  no difference

### 2. stacked-cuts-12 (−14.9 dB)

- **Same or different, which you prefer, what you heard:**  significantly less snare in A

### 4. stacked-boosts (−16.7 dB)

- **Same or different, which you prefer, what you heard:**  A is louder and a touch muddier

### 3. stacked-cuts-6 (−23.9 dB)

- **Same or different, which you prefer, what you heard:**  no audible difference

### 5. surgical (−30.8 dB)

- **Same or different:**  no audible difference

### 7 and 8, the dynamic pair (−40.8 / −43.4 dB, 3.8 / 4.7 dB GR)

- **Anything at all:**  no audible difference

---

## Source 7 — LOCKED IN, full mix  ⚠ LEVEL

**Every case here renders above 0 dBFS, up to +5.0.** The mix arrives at
−0.1 dBFS peak, so any boost puts it over. The files are 32-bit float and
nothing is clipped inside them, but a fixed-point output will clip.
**Pull the monitoring down 6 dB before this source and leave it there for all
eight cases.**

The strongest dynamic source in the set (10.0 / 12.0 dB), and the most like
the thing anyone would actually reach for DEQ with.

### 6. vocal-control — the validity check (−36.9 dB)

- **Could you tell them apart?** Should be no. If yes, say what gave it away -- and stop there, the setup is leaking:  no audible difference

### 1. lowcut-under-shelf (+0.1 dB — as loud as the input; peaks 3 dB apart)

The 808 said the same thing about this case. This is it on a finished mix.

- **Same or different, which you prefer, what you heard:**  bass is much lower in B

### 2. stacked-cuts-12 (−16.6 dB)

- **Same or different, which you prefer, what you heard:**  A is a much more noticeable cut

### 4. stacked-boosts (−23.6 dB; renders to +5.0 dBFS)

- **Same or different, which you prefer, what you heard:**  B is much smoother, A is harsher

### 3. stacked-cuts-6 (−25.1 dB)

- **Same or different, which you prefer, what you heard:**  same

### 8. tamer-dynamic (−35.1 dB, 12.0 dB GR)

- **Same or different, which you prefer, what you heard:**  significantly less sibilant in A

### 7. deess-dynamic (−34.6 dB, 10.0 dB GR)

- **Same or different, which you prefer, what you heard:**  A is more natural, B is more obviously de essed

### 5. surgical (−37.3 dB, expected inaudible)

- **Same or different:**  B is slightly more resonant in the high mid range

---

## The verdict

Four lines to comment on, once, after the cases above.

- **Does the difference matter on real material?** (no / on a few cases,
  which / yes, plainly):  Yes, on the cases where a filter is asked to do something a curve implies -- stacked cuts and boosts, and a low cut under a shelf. Inaudible on the rest.

- **Is anything about serial worse?** (nothing / what):  Nothing. No fault on either topology, and in round two the two audible pairs both went to serial.

- **Does anything argue for exposing a topology switch as a control?**
  Held open until this pass is heard; it is the one thing keeping DEQ's panel
  out of the UI pass. (no, serial only / yes, and what it would be for):  No, serial only. Every parallel preference was it doing less, which a knob reaches.

- **So the default is** (serial, as chosen on the measurements / parallel /
  something else):  Serial, as chosen on the measurements. Frosty, 2026-09-12.

- **Listened on** (machine, monitoring, date):  AURORA, 2026-09-11 and 2026-09-12.

---

## What the key says — opened 2026-09-11, after all 51 answers were in

Seed 7 gave **the same A/B assignment on every source**, so "A" meant the same
topology throughout:

| case | A | B |
|---|---|---|
| `lowcut-under-shelf` | parallel | **serial** |
| `stacked-cuts-12` | **serial** | parallel |
| `stacked-cuts-6` | parallel | **serial** |
| `stacked-boosts` | **serial** | parallel |
| `surgical` | **serial** | parallel |
| `vocal-control` | parallel | **serial** |
| `deess-dynamic` | parallel | **serial** |
| `tamer-dynamic` | parallel | **serial** |

*(A method note for the next round: one seed across every source means a
listener who forms an impression of "A" carries it from source to source. Use
a different seed per source next time. It did not bite here — the answers
track the physics case by case rather than following a letter — but it could
have.)*

### The controls all passed

Seven sources, seven controls, and not one was distinguishable: "no", "no",
"cant tell them apart", "cant tell", "can't tell", "no difference", "no
audible difference". So the playback chain was not leaking a difference, and
everything below it stands.

### Every description matched the physics

This is the part worth keeping. On each case where a difference was heard, the
direction was the one the measurements predict, without knowing which was
which:

| source | case | what Frosty heard | what the key says |
|---|---|---|---|
| 808 | `lowcut-under-shelf` | "B preferred, bass is much fuller" | B was **serial** — the cut works under the shelf, so what is left is the note rather than the rumble |
| mix | `lowcut-under-shelf` | "bass is much lower in B" | B was **serial** — on a mastered mix the same cut removes what is under it |
| drums | `lowcut-under-shelf` | "A has more low end" | A was **parallel** — its low cut is bypassed by the shelf's own path |
| vocal, drums, synth, gtr, room, mix | `stacked-cuts-12` | "A sounds totally bell filtered", "A sounds like it's been EQ'ed, B sounds unedited", "A has significantly less high end", "significantly less snare in A", "A is a much more noticeable cut" | A was **serial** — two −12 dB cuts stack to −24 dB; parallel sums to −6 dB and inverted |
| drums, synth, gtr, room, mix | `stacked-boosts` | "A has a much more audible boost", "A maintains high mid", "A noticeably louder/harsher", "A is louder and a touch muddier", "B is much smoother, A is harsher" | A was **serial** — +12 dB against parallel's +9.5 |
| mix | `deess-dynamic` | "A is more natural, B is more obviously de essed" | B was **serial** — the dynamic cut reaches the signal instead of being bypassed by the shelf path |
| synth | `stacked-cuts-6` | "A is audibly EQ'ed, B sounds unchanged" | A was **parallel** — its −52 dB notch is the audible one here, and serial's −12 dB is the gentle one |

### Where a preference went, and why

**Nothing was reported as a fault.** No pumping, no smearing, no clicks, no
artefacts — on either topology, on any source. The preferences that went to
parallel are all cases where parallel simply did *less*:

- `stacked-cuts-12` on the vocal — "B sounds exponentially better" — where
  serial's −24 dB is a deliberate extreme, and parallel's −6 dB is closer to
  untouched.
- `stacked-boosts` on the vocal and the mix — serial's +12 dB against
  parallel's +9.5.
- `tamer-dynamic` on the vocal and drums, and `deess-dynamic` on the mix —
  serial catches harder.

And the preferences that went to serial are the cases about a filter doing
what its curve says: the 808's fuller low end, the guitars' `stacked-cuts-6`,
the vocal's `deess-dynamic`.

Which is the distinction the decision turns on: **amount is a knob, behaviour
is a topology.** Every parallel preference above is reachable in serial by
asking for less; none of the serial behaviours is reachable in parallel at
all.


---

## Round two — the dynamics, level-matched (6 pairs)

Frosty's condition for settling on serial: *serial only if the parallel
preferences can be reached in serial by asking for less.* Measured on AURORA,
2026-09-11:

- **Tone: yes, comfortably.** `stacked-cuts-12`'s parallel result (−6.06 dB at
  1 kHz) is matched by one serial band at −6.8 dB to within **0.76 dB**
  anywhere in the spectrum; `stacked-boosts`'s +9.52 dB by one serial band at
  +10.15 dB, within **0.63 dB**.
- **Dynamics: the amount, yes; the movement, not quite.** Serial with a
  smaller range lands on parallel's level exactly, but the *envelope* still
  differs by up to **1.8–2.4 dB** momentarily, around the attack and release.
  In parallel the static band's path keeps feeding through while the dynamic
  band clamps, so the catch has a softer edge. No knob reproduces that.

These six pairs are that residual, and nothing else. `measure_deq --match`
reduced serial's range until the band an octave either side of the dynamic
band sits at **the same level in both** — 0.06 dB apart at worst. So the
question is no longer "which is doing more", it is **"does the way it moves
sound better or worse?"**

Seed 11, so the A/B assignment is fresh and unrelated to round one. Keys
unopened.

| source | case | serial's range | parallel's | band level matched to |
|---|---|---|---|---|
| m-vocal | `deess-dynamic` | −4.05 dB | −10 | 0.005 dB |
| m-vocal | `tamer-dynamic` | −7.75 dB | −12 | 0.024 dB |
| m-drums | `deess-dynamic` | −10.00 dB (no reduction needed) | −10 | 0.063 dB |
| m-drums | `tamer-dynamic` | −5.45 dB | −12 | 0.000 dB |
| m-mix | `deess-dynamic` | −5.54 dB | −10 | 0.003 dB |
| m-mix | `tamer-dynamic` | −8.20 dB | −12 | 0.005 dB |

**What to listen for:** not how much is being taken off — that is equal now —
but *how* it comes off and goes back on. Pumping, a soft edge against an
abrupt one, the first syllable or the first hit of a phrase, how the sound
settles after.

⚠ `m-mix` is the mastered mix again: monitoring down 6 dB.

### m-vocal — deess-dynamic

- **Different, which you prefer, what you heard:**  B preferred, i could hear the reduction on A, but it's close

### m-vocal — tamer-dynamic

- **Different, which you prefer, what you heard:**  couldnt differentiate

### m-drums — deess-dynamic

- **Different, which you prefer, what you heard:**  couldnt differentiate

### m-drums — tamer-dynamic

- **Different, which you prefer, what you heard:**  couldnt differentiate

### m-mix — deess-dynamic

- **Different, which you prefer, what you heard:**  B preferred, A audibly drastic on full mix

### m-mix — tamer-dynamic

- **Different, which you prefer, what you heard:**  cant differentiate

### After the six

- **Does the way it moves make a case for shipping both topologies?**
  (no, serial only / yes, and for what):  No. Four of six indistinguishable, and both audible ones preferred serial.

### What round two's key says — opened 2026-09-12, after all six were in

Seed 11: `deess-dynamic` was **A = parallel, B = serial** on all three sources;
`tamer-dynamic` was **A = serial, B = parallel**.

| source | case | what Frosty heard | which was which |
|---|---|---|---|
| m-vocal | de-ess | "**B preferred**, i could hear the reduction on A, but it's close" | B was **serial** |
| m-mix | de-ess | "**B preferred**, A audibly drastic on full mix" | B was **serial** |
| m-vocal | tamer | couldn't differentiate | A serial, B parallel |
| m-drums | de-ess | couldn't differentiate | A parallel, B serial |
| m-drums | tamer | couldn't differentiate | A serial, B parallel |
| m-mix | tamer | couldn't differentiate | A serial, B parallel |

**Four of six were indistinguishable. In the two that were not, serial was
preferred both times** — and the thing that gave parallel away was its
reduction being *more* audible, "audibly drastic on full mix", even though the
two were matched to within 0.003 dB in that band.

That is the opposite of what the measurement suggested to look for. Parallel's
catch was expected to have the softer edge, because its static path keeps
feeding through while the dynamic band clamps. What that actually sounds like,
on a de-esser, is the sibilance being pulled at while the shelf holds its
level up — and it reads as heavier handling, not gentler.

So the residual that no knob can reproduce is real, audible on two of six
pairs, and it favours **serial**.
