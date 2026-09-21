# Handoff: delay module groundwork

Paste everything under the line into a new session opened on this repo. It is
the same ruleset that produced `docs/1176-comp/` (PR #19) and is producing
`docs/deesser/`, adapted for a delay. Written on AURORA, 2026-09-20.

Before starting, the new session should work in **its own git worktree** on a
new branch `frosty-delay-groundwork` off `origin/main`: other sessions are
building `modules/fetcomp` and writing `docs/deesser/` in the main working
tree. Commit locally; do not push or open a PR until Frosty says so.

---

You are orchestrating, not implementing. Model discipline matters: you dispatch,
you do not research. Token budget is tight — follow the rules literally.

GOAL
Produce a groundwork pack for a delay module for the BMO Rack plugin (plugin
code `Bdly` is reserved for it in `products/AGENTS.md`). Spec, theory, math and
test direction only — NO DSP implementation code. Our dev team writes the code
from this pack.
Repo: the current working repo. Output dir: docs/delay/ (exists; this handoff
file lives in it).

HARD RULES
- You read nothing but a top-level directory listing and the agents' returns.
- Every agent writes its findings to its assigned file and returns ONLY: file
  path, <=150 words of bullets, and any blocking unknown. Never ask an agent to
  paste file contents back. Never re-read a file an agent has summarized.
- If an agent overruns its word cap, accept it — do not request a rewrite.
- Two waves total. No third wave, no "one more agent to check X" unless an agent
  reported a blocking unknown by name.
- Do not narrate between tool calls. No progress summaries.
- At most ONE clarifying question to me, before launching anything, and only if
  the repo module layout is genuinely ambiguous.
- Agents never commit, push, switch branches, build the rack plugin target, or
  write outside docs/delay/. Wave 1 agents A2 and A3 get no repo access; A1 gets
  no web access.

WAVE 1 — launch all three in a single message (Sonnet)
A1 "repo recon (delta)": a general conventions document already exists at
   docs/1176-comp/00-repo-conventions.md on branch `frosty-fetcomp-groundwork`
   (PR #19; read it with `git show origin/frosty-fetcomp-groundwork:docs/1176-comp/00-repo-conventions.md`
   if it is not on main yet). Reference it, do not repeat it. Document only the
   delay-specific delta: reusable DSP already in the repo (delay lines or
   fractional-delay/interpolation code anywhere — Dimension, Tune, Util, the
   dry-path delay matching in modules/eq and modules/sat, core/dsp/Oversampler.h,
   shared filters, the Saturator's residual-ADAA shaper for a feedback-path
   saturator), whether the module interface gives a module host tempo / transport
   / time-signature (needed for tempo sync — say exactly what `ModuleDsp`
   receives and what would have to be added if nothing), how tail length is
   reported to the host if at all, how latency vs. wet delay time is kept
   distinct, stereo handling conventions, the `Bdly` reservation line, the
   identity-row pattern, and which accent hue gaps are still free (BMO FET took
   `#5489d4` ~215°; the de-esser will take one too). -> docs/delay/00-repo-conventions.md,
   <=800 words.
A2 "reference behavior": documented behaviour of the classic delay families —
   tape echo (wow/flutter, head bump, HF loss per repeat, saturation, self-
   oscillation, speed-change pitch glide), bucket-brigade analog delay
   (companding, clock noise, anti-alias/reconstruction filter darkening, limited
   delay time), early digital delay (bit depth, sample-rate limits, modulation),
   and modern clean/ping-pong/dual delays. Typical time ranges, feedback limits,
   filter corner frequencies, modulation rates/depths, ducking behaviour, tempo-
   sync note values incl. dotted and triplet. Cite sources; flag folklore vs.
   measured. End with a table of target figures with confidence. No repo access.
   Brand and product names only inside citations. -> 01-reference-behavior.md,
   <=1000 words.
A3 "design-approach survey": fractional-delay interpolation (linear, Lagrange,
   Thiran allpass, windowed sinc, Hermite) with HF loss, modulation artefacts and
   CPU for each; time-change behaviour (pitch-glide "tape" vs crossfade "digital"
   vs slew); feedback-loop design (filters, saturation and its antialiasing in a
   recirculating loop — aliases accumulate per repeat — DC handling, stability at
   feedback >= 100%, limiter in loop); tempo sync and host-tempo smoothing;
   ping-pong/cross-feedback stereo topologies; ducking detector; modelling of
   tape/BBD character (wow/flutter LFO + noise models, companders, BBD clock
   filters) from published work (DAFx/AES: Välimäki et al. on fractional delay,
   Raffel/Smith and Holters/Parker on BBD, tape-echo modelling papers); CPU,
   memory, latency and oversampling implications. Comparison table and a neutral
   shortlist. No repo access. -> 02-design-approaches.md, <=800 words.

WAVE 2 — after Wave 1 returns, launch both in a single message (Opus)
B1 "DSP spec": choose and justify a topology given A2/A3 and our constraints
   (zero reported latency, plugin-grade CPU, many instances in a rack, 44.1–192
   kHz). Cover delay-line and interpolation math, time-change law, feedback loop
   math and stability bound, in-loop filters and saturation with antialiasing
   strategy, modulation, ducking, tempo-sync mapping (dependent on A1's finding
   about host tempo), stereo modes, mix/dry path, tail behaviour on bypass and
   parameter smoothing, maximum delay time and memory, coefficient derivation,
   fixed parameter values to target (each traced to 01 or marked CALIBRATE).
   Math and prose, no code. -> 10-dsp-spec.md, <=2000 words.
B2 "integration + test direction": how the module drops into the conventions in
   00 (module id, display name, identity row, registration touch points, accent
   candidates), parameter/automation layout (ids, ranges, defaults, stepped vs
   continuous, what is permanent once shipped — tempo-sync note-value choice
   lists are append-only, so get their order right), and how the devs should
   BUILD the test suites (not the suites themselves): delay-time accuracy incl.
   fractional times, interpolation frequency response, time-change artefacts,
   feedback decay rate and stability at maximum feedback, self-oscillation
   bound, saturation THD and accumulated aliasing floor over N repeats, ducking
   timing, tempo-sync accuracy across tempo changes, mix/null tests, bypass
   tail, sample-rate and block-size invariance, denormal/NaN/silence
   robustness, CPU, memory and latency acceptance criteria. Panel and visual
   verification: reference docs/1176-comp/11-integration-and-test-plan.md for
   the render/snapshot/inspect tools rather than re-deriving them.
   -> 11-integration-and-test-plan.md, <=1200 words.

FINISH
Write docs/delay/README.md (<=400 words): one-line index of each file plus the
top 5 open decisions I need to make. Reply to me with that file path and those 5
decisions only. Then stop.

Naming: refer to reference devices generically ("tape echo", "bucket-brigade
delay"). Do not use hardware or software brand or product names in code, docs or
UI strings; citations only. Name the machine the work happened on (AURORA unless
Frosty says otherwise). Never put Windows account names or user paths in docs.
