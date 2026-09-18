# The Ableton pass — session brief

**For the session that runs Frosty's host pass on the UI-pass build.** Read
this, then `ui-pass-checklist.md` for what each panel still owes, then the
per-module checklists named below. Prepared on **ICE QUEEN**, 2026-09-17,
from the review of pull request #9 on `kevkloud/bmo-mix-rack`.

The UI pass was a **measured** pass: every claim in `ui-pass-*.md` is a render
or a number. Nothing in it has been heard. This file is the list of what the
ear still owes, and it is longer than PR #9's description says.

---

## 1. What is installed, and that it is the right thing

All nine bundles in `C:\Program Files\Common Files\VST3` on **ICE QUEEN** were
verified on 2026-09-17 as **byte-identical** to the `BMO-Windows` artifact of
CI run **35290922537** (`ui-pass` at `102f6571`, green on all four jobs). That
commit differs from PR #9's head `445b4af` by one `.md` file only, so what is
installed is what the pull request contains.

No Debug build has clobbered them — `scripts/build.sh` installs over this
folder, so re-check by hash, never by size, before trusting a listening result.

| bundle | SHA-256 of `Contents/x86_64-win/<name>.vst3` |
|---|---|
| BMO CEQ | `410d8ebc5a741b0619e5171d20fe8f99a55362ae1355f7370bdb3793284208d8` |
| BMO DEQ | `a8d5aa9ce7664f19f27b3645e050a23df9a9b3055bc691fb3cfb72632d1a507c` |
| BMO Dimension | `b384e72e2a4bb04e3aecaeea5430abbf9f9d28315a060dcdab87f5fea6ddb8f6` |
| BMO Mix Rack | `ffbaa12bd21088870d7e9a292296d41f8eb56d9dbec1d28c899781967b7c78f3` |
| BMO Opto | `b2d18a7271fdd301763100438d73f8b7461334ad66ed0bc40712863abf31a0b8` |
| BMO Saturator | `03d28963a451c93ca6a92b5aa3362453d96cee8f0749878413e0739e88f8cc43` |
| BMO Tune RT | `b2d26710875718bde62fcd8ac2c56a4ccae8b246a84293127048da710e4d2110` |
| BMO Util | `9bc5143ef699b73a6753b0223ab915356e5398b8c754ef8cc7ad7df5157c0303` |
| LTV Comp | `d7f6fcc436fa23dc7d9f6363249a607b7e36e8db88a15e167f0c77dd84151972` |

There is **no stale `BMO EQ.vst3` or `BMO Vcomp.vst3`** on ICE QUEEN; both
renames landed cleanly here. Do not assume the same of AURORA — the 0.2.4
install there on 2026-09-14 put `BMO EQ` and `BMO Vcomp` in place under those
names, and nothing has removed them. Check before listening on that machine.

---

## 2. What has to be heard

Six changes in this build alter what comes out of the plugin. PR #9's body
flags only the first, and miscounts it as two.

| # | Change | Module | Where it came from | Notes |
|---|---|---|---|---|
| 1 | `asymmetry` sign flipped: **+ now favours the right** | Dimension | `a95db6c`, UI pass | On `dim-testing-checklist.md` §2. No factory preset sets it; a session that automated ASYM leans the other way now. |
| 2 | **CENTS at 0 is now off** — was a static comb with DETUNE on, whose level depended on where the sweep had got to | Dimension | `240de6d`, 0.2.4 review | Injected difference is scaled by the first cent, on the same 8 ms. |
| 3 | **Phase moved onto the blend** — flipped the wet path only, so a flat EQ cancelled against its own dry copy at Mix 50 %, and did nothing at Mix 0 | CEQ | `240de6d`, 0.2.4 review | Matches what the Saturator already did. |
| 4 | **Polarity and MONO ride the 5 ms smoother** — were applied per sample the instant the parameter changed | Util | `240de6d`, 0.2.4 review | Switch over signal, not silence: the bar is *measurable*, not *audible*. |
| 5 | **"Keep The Air" is AMOUNT 35**, was 70 — a +19 dB boost above 6 kHz into the limiter | LTV Comp | `240de6d`, 0.2.4 review | Same figure and reason as Keep The Chest. |
| 6 | **THRU bands are capped** — the thru path takes the gain the curve would have given that content, not the whole makeup | LTV Comp | `vcomp-thru-cap-2026-09-14.md` | Tilt across AMOUNT 4.6/8.8/12.9/17.7 dB → 2.4/1.7/1.1/0.6; pumping −1.5 → −0.03. |

**Item 6 carries an open question a measurement cannot answer**: does the low
end still arrive at AMOUNT 70–90 with LOW THRU at 150–300, or does capping it
make LOW THRU feel like it stops working at the top of the knob?

### Not in scope, and why

- **ROTATE / TURN.** Its sign was flipped on 2026-09-09 in `0d2d659` and
  **confirmed by ear** in that day's Ableton pass — "`-` moved the image right
  and `+` moved it left, the opposite of a pan knob". That flip is already in
  `kevkloud/main` and predates this pull request. Only `asymmetry` is new and
  unheard. Both knobs have been flipped; only one of them needs listening to.
- **BMO Opto.** Nothing in `modules/opto/` changed. The lift of its gain
  computer into `core/dsp/GainComputer.h` (`a83507c`) is a pure refactor —
  `opto_dsp` passing is the check, and it passes.
- **DETUNE's fade-out and instant-on.** Heard and passed on ICE QUEEN,
  2026-09-11, build `6774E4CC`.

---

## 3. What the panels still owe

`ui-pass-checklist.md` §B is the authority. The pass covered six of the eight
rack panels with their own record; **LTV Comp and the rack itself have no
record and mostly open items**, and BMO DEQ has a record with none of its four
checklist items ticked. All eight are to be walked before the pass is called
done — see the checklist's own §D.

---

## 4. Ground rules that have bitten before

- **Levels.** Tracks sit at −18 dBFS RMS / −12 peak. −6 dBFS is a mix-bus
  level, not a track level.
- **Switch over signal, not silence.** A click that only shows on a switched
  silence is not the test.
- **Say which machine.** Record every listening result against ICE QUEEN or
  AURORA by name, with the bundle hash it was heard on.
