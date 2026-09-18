# 0.2.4 installed on AURORA — run 34891007014

Installed on **AURORA** on 2026-09-14 from CI run **34891007014**
(`workflow_dispatch` on `integration` at `e4082c3`), over the nine bundles that
were there. Every one verified byte-for-byte against the artifact after the
copy.

CI was green on all four jobs: DSP 1m21s, Each side alone 1m17s, Windows
40m34s, macOS 9m0s. **macOS passed this time** — the previous run's macOS
failure was the runner not being acquired, not a build fault. Local Release
`ctest` was 26 of 26 on AURORA before the dispatch.

`Version` in every `moduleinfo.json` now reads **0.2.4**. It read 1.0.0 in
every previous tester build; that was one of the review's fixes and this is the
first install where the plugin tells the truth about what it is.

| bundle | SHA-256 of `Contents/x86_64-win/<name>.vst3` |
|---|---|
| BMO DEQ | `3549dcdf1061ea68edf7c3cca018df5e802e24d683c3063dd2ebec9a005d8346` |
| BMO Dimension | `7b99ca3b8659fb556086c98fb47273154b2b5fbc5badbb82ddc9a3d31a4c1013` |
| BMO EQ | `79fd9acfb8869aa5fff968e08eb681e957721f1b057681129a118e0936d97d23` |
| BMO Mix Rack | `bf5a5a6fa1c715046b21cc6e16b5c4b80557ebe4b8dc03dd43f0fb6bb793c580` |
| BMO Opto | `2677bfc904784270257f544fd4a6c8f295c5a991772169a6fadeb12d4c4dd813` |
| BMO Saturator | `dd2f6d532085db5cdc896336c0259d36034755b18ce3ac0c1d0c4d68bd44fe17` |
| BMO Tune RT | `80da9b4211da65ba6c770e85618348c5bbd4ad1aad19210ade78c33d86818271` |
| BMO Util | `81e8c76fcc4330f4e65f1aef2bbc87a07aafe4ffd940496992bea0f6e655f5a3` |
| BMO Vcomp | `a8bc3243fe109d25eddbd6be437f504076b6617805fd7b89138c41cfbf7240c4` |

VST3 only. No standalone builds are installed on AURORA and none were added;
the artifact carries nine of them if they are ever wanted.

## What changed under the ear since the last install

The previous install was `BMO-Windows` from run 34834557823 at `cbd0939`. The
six faults that install was known to have — and which the listening checklists
told Frosty to ignore — are **all fixed here**:

- EQ Phase cancelled at Mix 50 %
- EQ Auto Gain was inert until a band moved
- Util's polarity and MONO switches clicked
- Dimension's CENTS at 0 was not off
- Vcomp's "Keep The Air" spat (a +19 dB sibilance boost into the limiter)
- every plugin reported version 1.0.0

New since that install, and not yet heard by anyone:

- **Vcomp's THRU bands are capped.** The thru path takes the gain the curve
  would have given that content had it been compressed, instead of the whole
  makeup. Tilt across AMOUNT goes 4.6/8.8/12.9/17.7 dB to 2.4/1.7/1.1/0.6 and
  pumping −1.5 to −0.03. `testing-notes/vcomp-thru-cap-2026-09-14.md`. The one
  question a measurement cannot answer: **does the low end still arrive** at
  AMOUNT 70–90 with LOW THRU at 150–300, or does capping it make LOW THRU feel
  like it stops working at the top of the knob.

**BMO Tune RT changed in this install.** The copy that was on AURORA until now
was the 2026-09-10 build, left alone deliberately so the Vcomp work could not
disturb Tune's own pass. It is now the guard-6 build from `integration`, which
is the build round nine confirmed — so for the first time every bundle on this
machine comes from one commit.

Nothing in `modules/opto/` changed: the attack candidates were rejected.
