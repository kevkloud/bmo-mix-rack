# The blind listening protocol

**How a BMO listening decision is run.** Written 2026-09-12 on AURORA, after
BMO Tune RT's three rounds and BMO DEQ's serial-vs-parallel pass (57 pairs,
seven sources). Everything here is something that either worked or went wrong
at least once; nothing is hypothetical.

Use it whenever a decision needs ears: a topology, a voicing, a fix that has
to be judged against what it replaced. It is not for checklists in a DAW —
`*-testing-checklist.md` is that shape — it is for **A/B where the listener
must not know which is which**.

---

## 1. The shape of a round

1. **Render pairs with the real DSP**, not a stand-in, from a tool in the tree
   that drives the shipping code path.
2. **Shuffle A/B per case from a seed**, and write a `key.txt` beside the
   audio saying which was which.
3. **Write the answers sheet** with one comment line per pair, and the
   measured figures beside each so the listener knows what to expect.
4. **The listener comments; the session writes the comments into the file.**
5. **Open the key only when every line has something on it**, then decode the
   answers against it in the same file.
6. **The verdict is the listener's**, in their own words, recorded with a date
   and a name.

A round that ends without a decision recorded is a round that will be run
again by someone who does not know it happened.

## 2. The audio

- **Live outside the repository**, in a short path. `C:\Users\<user>\<thing>-blind\`
  works; a path inside a repo folder does not, and Windows stops at 260
  characters. Tools should refuse a folder they cannot use rather than write
  half a set.
- **32-bit float**, so a render that goes over 0 dBFS is not clipped in the
  file — but say so in the sheet, because the listener's output will clip.
- **Deterministic**: same source, same seed, same build, byte-identical files.
  That is what lets the other machine re-render instead of copying 40 MB a
  source, and what makes a result reproducible a month later.
- **A different seed per source.** One seed across every source gives the same
  A/B assignment throughout, so a listener who forms an impression of "A"
  carries it from source to source. DEQ's round one made this mistake. It did
  not bite — the answers tracked the physics case by case — but it could have.

## 3. Before anyone listens: check the set is worth listening to

Three checks, each of which has caught something real:

- **Read the input's RMS and peak.** Two of DEQ's reference bounces were
  well-formed 32-bit float WAVs of the right length in which every sample was
  zero. The tool read them as −300 dBFS; a byte scan confirmed it. A bounce
  can be the right length and format and still be empty.
- **Check that the thing under test actually engaged.** A dynamic case whose
  gain reduction reads 0.0 dB on that material is two static filters: it
  answers nothing about dynamics, and the sheet should say so rather than
  inviting a verdict from it. On DEQ, dynamics engaged on five of seven
  sources.
- **Check the levels.** A mastered mix arrives at −0.1 dBFS, so any boost puts
  the render over. Say which cases are hot, and by how much, at the top of the
  sheet.

## 4. Include a control

**Every source gets a pair that should be indistinguishable**, rendered from
settings whose measured difference is far below audibility (DEQ used four
separated, moderate bands: −32 to −41 dB relative to input).

If the listener can tell the control apart, something other than the thing
under test is reaching their ears — a level difference, playback order, a
click at a file start — and **nothing else from that source can be trusted**
until it is found. Put the control first in each source's section, and say
what it is for.

On DEQ, seven sources gave seven indistinguishable controls, which is what
makes the other answers worth anything.

## 5. The answers sheet

Its format matters more than it sounds, because it decides how much work the
listener does per pair.

**The Files pane is read-only — there is no save button and checkboxes cannot
be ticked.** Tune's rounds solved this and DEQ's first sheet ignored it and
had to be rebuilt: the listener **comments on a line**, and the session writes
the comment into the file.

So: **one bold line per pair**, and nothing else to fill in.

```markdown
### 2. stacked-cuts-12 — two −12 dB cuts at 1 kHz (−7.3 dB; peaks differ by 4.3 dB)

- **Same or different, which you prefer, what you heard:**
```

The rest of the file's shape, all of which earned its place:

- **A progress table the session fills in**, not the listener — source, how
  many cases, done. Over 51 pairs it is what tells everyone where a session
  stopped.
- **Cases ordered loudest-difference-first within each source**, with the
  measured figure in the heading. The listener spends attention where there is
  something to hear.
- **Cases the measurement says are inaudible collapse to one line** —
  "**Same or different:**" — so clearing them is quick and they are still on
  the record.
- **A warning where a case renders hot**, in the source's own heading as well
  as the top of the file.
- **A line saying that a fault the question does not ask about should be said
  anyway** — a click, a level jump, anything that sounds broken rather than
  different.
- **Verdict lines at the end**, phrased as the questions the round exists to
  answer, one of which should be "so the decision is".
- **"Same" is a real answer.** Say so. Most pairs on most material should be
  hard to tell apart, and a sheet that reads as though difference is the
  expected answer will get difference.

## 6. Decoding

Write the decode **into the same file**, under the answers, never in chat
alone:

- The A/B table from the key.
- A table of **what the listener said against what it was**. This is the part
  worth the most later: on DEQ it showed that every heard difference ran in
  the direction the measurements predicted, blind, which is a stronger result
  than the preferences themselves.
- **Where each preference went and why.** Separate "X was preferred" from "X
  did less of something", because they lead to different decisions.

## 7. When a preference is really about amount

If the listener prefers the side that is doing *less*, the round has not
answered the question yet — it has measured a knob position.

Run a second round with the two **matched for amount**, so the only thing left
is behaviour. DEQ's `measure_deq render --match` does this by bisecting one
side's range until both sit at the same level, and the key records the value
it found.

- **Match where the thing works, not across the whole file.** A Q 4 bell at
  2.5 kHz moves a mix's RMS by hundredths of a dB while moving its own band by
  ten. The first cut of `--match` matched on whole-file RMS and "matched" one
  case at its own setting, matching nothing. It matches on the energy an
  octave either side of the band now.
- **Say in the sheet what is left to hear.** For DEQ: not how much comes off,
  which is equal now, but how it comes off and goes back on.

The distinction this round settles is worth stating in whatever it decides:
**amount is a knob, behaviour is a topology.**

## 8. Two machines

- The audio does not travel; **the blank sheet does**. Keep a copy in
  `testing-notes/`, put it beside the audio as `ANSWERS.md`, fill it in there,
  and commit the filled copy back over the blank one. That is what carries a
  result between AURORA and ICE QUEEN.
- **Name the machine** in the run record and in the verdict.
- Re-render rather than copy where the renderer is deterministic.

## 9. What the record looks like afterwards

Three files, and each has a job:

| file | holds |
|---|---|
| `<thing>-<question>-listening.md` | the method and the argument: what the cases are, why those, what the measurements already settled |
| `<thing>-blind-<date>.md` | the run: what was rendered from what, the tool's measured table per source, and the result |
| `<thing>-blind-answers.md` | the sheet, the answers as given, the decoded key, and the verdict |

The decision itself goes where the decision lives — the spec, `decisions.md`,
the module's `AGENTS.md` — with a date, a name, and a link back to the run.
BMO DEQ's serial-vs-parallel pass is the worked example of all of the above.
