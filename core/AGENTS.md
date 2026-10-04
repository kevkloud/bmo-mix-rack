# core/

Shared code. Four layers, each depending only on the ones above it:

```
dsp/      JUCE-free. ModuleDsp (the interface every module's DSP implements),
          Oversampler, Meter, FiniteGuard, SwitchFade (a hard switch made a
          short fade). Nothing here includes <juce_*>.
state/    ParamSpec (a parameter described without JUCE), ParamSet (a spec
          list bound to live juce parameters), Parameters.h (APVTS layout
          from specs), PresetManager (files, factory lists, migration).
ui/       Tokens (colours; theme JSON hot-reload), Fonts, LookAndFeel,
          Controls (PlainKnob, ConcentricBand, SwitchButton, OutputMeter),
          PresetBar, ProductHeader, ModulePanel (the base every panel extends),
          ExpandButton (a two-width module's switch, on the host's bar).
product/  ModuleDef (what a module exposes), ModuleEngine (spec values ->
          DSP), SingleModuleProcessor + ProductEditor (a module as a plugin),
          HostTempo (the block's tempo, read from the host's playhead).
rack/     SlotParameter (one generic host parameter, remapped live),
          SlotOverflow (a module's parameters past the 32nd, off the host
          grid), RackProcessor (8 engines in series), RackEditor.
```

## Rules

- `dsp/` must build with `BMO_DSP_ONLY=ON`. If you need JUCE, it does not
  belong here.
- **A switch that changes the signal path fades** (`dsp/SwitchFade.h`): a
  step that measures gets a fade, heard or not, and the bound is that the
  largest sample-to-sample step after a switch stays under 1.5x the steady
  signal's own. `Ramp` + `crossfade` blend an old path into a new one;
  `Dip` fades to zero around a change no blend can cross, such as a latency
  change. All three are bit-exact while idle -- `crossfade` at 0 or 1
  returns the selected path's sample itself, -0.0 and a NaN on the other
  path included, and a `Dip` rests at exactly 1 from construction -- so
  adopting them leaves a module's steady output untouched;
  `switch_fade_tests` holds the contract.
- `ParamSpec::toNormalised/fromNormalised` must agree with
  `juce::NormalisableRange` for the same range: standalone products use
  JUCE's, the rack uses ours, and `RackTests` checks they match. Do not add
  skew or non-linear ranges to one without the other.

  Logarithmic ranges (`ParamSpec::logParam`, for BMO DEQ's frequency, Q and
  times) are how that is done: the law is written once, in ParamSpec, and
  `rangeFor` in `state/Parameters.h` builds every JUCE range -- standalone and
  rack slot alike -- with its mapping handed back to the spec. A linear spec
  still gets JUCE's own `{min, max, step}`, byte for byte what the golden
  schemas recorded. Build a JUCE range from a spec through `rangeFor` and
  nowhere else.
- A module may have **two widths** (`ModuleDef::expandedWidth`; only BMO DEQ
  does). It opens expanded standalone and compact in a rack, the host's bar
  carries the switch (`ui::ExpandButton` -- never the panel, whose controls
  all change the sound), and the panel lays itself out from the width it is
  given. The view is saved with the session as a `view` attribute on the
  module's PARAMS element, written by `getStateInformation` only: never by
  `captureState`, which preset files are also made from. In the rack it
  travels with the module's engine, so it follows the module through chain
  edits. `RackTests` holds all of this.
- **Knobs carry no numbers** unless a module opts in with
  `PlainKnob::setShowsValue` (only BMO DEQ does, by Frosty's call). The text
  is the host's own (`getCurrentValueAsText`); `setValueFormat` may shorten it
  for paint only. Either way `captionOverflow` measures the value at both ends
  of the range and its default, so `ui_layout_tests` covers it. `setFaceScale`
  shrinks a small knob's cap so its track (a fixed `Tokens::trackGap` out)
  stays inside it.
- `ConcentricBand::setLegend` puts words on a stepped dial in place of the
  spec's choices (DEQ's BELL / LS / HS / LC / HC). `legendOverflow` measures
  them against the 38 px legend box, and `ui_layout_tests` checks every dial.
- A **choice parameter whose positions are names rather than amounts** gets
  `ui::ChoiceBox`, a dropdown with its caption underneath: a knob says less and
  more, and Chamber is not more than Room. BMO Linger's TYPE and ER MODE are
  the two. `BmoLookAndFeel` already themes `juce::ComboBox` and
  `juce::PopupMenu` against the tokens, so the wrapper sets only the arrow,
  which the shared scheme leaves in the utility azure. `captionOverflow`
  measures the caption *and the widest item*, and `ui_layout_tests` checks
  every dropdown. A choice whose positions are an ordered amount stays a knob
  — and is better off a stepped float, which normalises without the
  index/(n−1) trap.
- A module whose **parameters write each other** supplies a
  `ModuleDef::createParamLink` (`state/ParamLink.h`), and `ModuleEngine` builds
  one per running module — so it works in the standalone plugin and in every
  rack slot, with or without an editor open. **BMO Linger's TYPE is the only
  one**: selecting a type re-applies that type's ten constants, so a type is a
  voicing rather than a table lookup. The writes go through `ParamSet::apply`,
  the path a preset recall already uses, and reach the parameters on the
  message thread through a `juce::ParameterAttachment` — never from the audio
  thread, which is where automation delivers the change that triggers them. A
  state restore goes through `ModuleEngine::restoreState`, which tells the link
  once the last value has landed (`ParamLink::stateRestored`): off the message
  thread the attachment only queues the TYPE write, and the late call used to
  stamp the type's block over the levels the session had just restored. The
  field is null for every module but BMO Linger. A second one has
  to argue for itself the way `modules/reverb/AGENTS.md` argues for the first.
- **Mono in, stereo out is opt-in per module** (`ModuleDef::acceptsMonoInput`,
  Frosty's decision on 2026-09-23). The bus contract is one function,
  `product/BusLayouts.h`, which both processors answer from: mono to mono and
  stereo to stereo for everyone, stereo to mono for no one, and mono to stereo
  only for a module that sets the flag -- **BMO Linger alone** -- or for the
  rack when any module it can host does, because a host fixes the layout
  before there is a chain. Where the layout is in use the input is duplicated
  into both channels, never cleared. The flag is the last field in
  `ModuleDef` and false for every other module, which keeps each of them on
  exactly the layouts it had before; `bus_tests` holds the table per product.
- **A host's bypass is the processed path with the modules taken out**
  (2026-10-03), in both processors: the input widened exactly as
  `processBlock` widens it, delayed by the latency the host was last told
  (`product/BypassDelay.h`, sized in prepare), then scrubbed. `processBlock`
  feeds the same line every block, so the switch into bypass is in step with
  what the processed path was producing. JUCE's default handed the input
  back undelayed and cleared the right channel on mono in / stereo out;
  never fall back to it. `bus_tests` holds both, for several chains and
  oversampling settings and across a latency change while bypassed.
- `SlotParameter::assign` keeps a pointer into the module's static
  `specs()` vector. Never hand it a temporary.
- A slot's `SlotOverflow` is an `AudioProcessor` only so that its
  parameters have an index; JUCE asserts on a gesture without one. Never add
  it to a host, a graph or an editor. `rebuild` makes one for a module
  arriving in a slot, new or moved there, because its parameters are named
  for the slot; it is destroyed after its engine, and a slot's `overflow`
  member is declared before `engine` for that reason.
- **A chain edit keeps every engine it does not remove or replace**
  (2026-10-03). Add, remove, move and replace build only the modules they
  bring in; every other module keeps its `ModuleEngine` -- DSP state, tail,
  values, overflow, view -- and a moved one takes it to its new slot, where
  `ModuleEngine::rebind` points it at that slot's lanes after its values are
  copied across. A restore and a rack preset still build every engine new.
  **What a module may now rely on:** an edit that leaves it in the chain
  never calls its `prepare` or `reset`, never rebuilds its DSP, and hands it
  every block, with the host's tempo, through the edit. What it may not: its slot number, which an edit
  changes, or its link object, which a move rebuilds on the new lanes.
- **An edit reaches the audio through a dip, and the audio thread never
  waits for it** (`RackProcessor::rebuild` has the whole design). The
  message thread builds the new chain at once and publishes it as a list of
  engine pointers; until the audio thread swaps to it, every block runs the
  chain it has on held values (`ModuleEngine::processHeld`), so nothing it
  reads moves under it. The output fades to zero over `kEditDipMs` (5 ms)
  with the bottom on a block's last sample, the next block swaps at its first
  sample, and fades back up: 10 ms and one sample of zero, at any block size.
  Removed engines are retired, not destroyed, until the audio thread has
  finished a block without them. **An engine the edit brings in arrives
  warm** (`RackProcessor::runWarming`): while the output fades out, it runs
  unheard on what its slot will be fed -- the running chain's output at that
  point, kept per engine -- so at the swap a module with latency is
  mid-stream rather than handing out its latency's worth of zeros. That
  costs the arriving engines' processing for the 5 ms: at 192 kHz / 32 with
  every one of eight slots replaced, about 150 us a block against 100 us
  steady and a 167 us period. A block larger than prepare promised skips
  the warming. `RackTests` holds an untouched compressor, delay and reverb
  sample-exact with a never-edited rack after the dip for add, remove, move
  and replace; and for BMO EQ at 2x and 8x and BMO Saturator at 2x added,
  swapped in or moved at 48 and 96 kHz and blocks of 32 and 512, the step
  under 1.5x, no block 1 dB over, a move sample-exact within 30 ms and an
  arrival within 1e-4 of a never-edited rack by 250 ms -- not to the bit,
  because 5 ms of warming is not an engine's whole history and the rest
  decays at the module's own rate.
- Everything that changes the chain goes through `rebuild`, which calls
  `rackChainWillChange` before and `rackChainChanged` after, synchronously.
  An engine an edit takes out is destroyed some time after the first call,
  never before it, so a listener that drops every panel there is safe; a
  kept engine is the same object afterwards. Keep it that way.
- The audio thread takes the rack's `chainLock` only as a try-lock, and only
  prepare, release and an edit made while no block has come for 200 ms (or
  four blocks) ever hold it; a block that meets it goes out silent, never
  dry. Everything else that reads or changes the chain off the audio thread
  serialises on `editLock`, which the audio thread never takes. Never block
  the audio thread on either.
- **No processor hands a host, and no module's DSP is ever handed, a NaN,
  an infinity or a sample at or over +192.7 dBFS** (2026-10-03;
  `finite::kCeiling`, whose comment has the measurements: one finite
  +200 dBFS sample held BMO Opto 40 dB down for a minute).
  `ModuleEngine::process`, which every standalone product, every rack slot
  and BMO Tune RT go through, replaces each such input sample with zero,
  and resets a module that produces one and silences that block, so it
  runs again from the next. The processors scrub the paths
  that skip an engine: the rack at its input, which covers an empty chain,
  and both processors'
  `processBlockBypassed`. On audio under the ceiling all of it only reads,
  so the output is bit-identical (about 90 ns a stereo 512 block per
  engine). **So a module needs no guard of its own and must not add one**:
  a second policy inside a module can only disagree with this one. BMO
  Dwell, BMO Tune RT, BMO FET (`FetCell.h`) and BMO DEQ (`Dynamics.h`)
  carry checks from before it; they are redundant, not a pattern.
  `finite_tests` walks the registry, Tune included, and the processors'
  bypass and skip paths, and holds all of this.
- **Every module is handed the host's tempo, once per block**
  (`ModuleDsp::setTempo`), so a module can sync to it without either
  processor knowing which modules care. Both processors read the playhead at
  the top of `processBlock` through one function, `product/HostTempo.h`'s
  `readHostTempo`, and pass the result through `ModuleEngine::process`, which
  gives it to the DSP after `setParams` and before `process`. The rack reads
  it once and hands every slot the same value, so two synced modules cannot
  disagree within a block; a block its try-lock skips gets no tempo, as it
  gets no audio. Three values and no more: bpm, valid, playing. **The three
  fallbacks are one case, indistinguishable by design** -- no playhead, no
  position, no bpm inside the window all arrive as bpm 0.0, valid false,
  playing false, even from a host that says it is playing without a tempo;
  a module that wants the transport alone needs a hook of its own. **The
  window is 10 to 999 bpm, both ends included** (Frosty, 2026-10-01;
  `kMinHostBpm`/`kMaxHostBpm`), and it is a validity window, not a clamp: a
  tempo outside it (zero, negative and non-finite included) is refused, not
  pulled to the edge, so a module never runs at a tempo the host did not
  report. A module may rely on a valid bpm being inside it, and still owns
  its own musical limits. A stopped transport is not a fallback: it keeps
  its bpm, with valid true and playing false. Holding the last tempo,
  falling back to a time parameter and not flushing on stop are the
  module's policy, never the plumbing's, which is why nothing here
  remembers a tempo. A held tempo survives a chain edit that keeps the
  module, moved or not, since the DSP is the same object (2026-10-03).
  **What a module must not assume:** that it survives a restore, a rack
  preset or a replace, which build a new engine that is handed the tempo
  again on its first block -- which matters only while the host's tempo is
  invalid; or that `prepare` comes with a tempo (the first `setTempo` comes
  with the first block).
  The default does nothing, and `tempo_tests` holds every registered module
  byte-identical with and without a playhead. Position, time signature and
  loop points are deliberately not carried; a beat-anchored module gets its
  own defaulted virtual rather than this one growing wider.
- **Two surfaces: Simple and Textured** (Frosty, 2026-09-25). Simple is the
  default and is the suite's flat look -- though not pixel-for-pixel what it
  drew before this pass: the dotted knob tracks, the 270-degree stepped
  sweep, the inked captions and the inside borders changed it on purpose
  (`docs/ui-material-proposal.md` lists them); Textured shades the
  same tokens -- a brushed or powder plate, knobs with form, switches that
  press in, rules, brackets and buses engraved. It is a machine-wide
  preference in `UI.json` beside the appearance (`ui::surface`), never a
  parameter. It may change **no colour, size or position**: anything that
  would is not a surface. A line's house finish is `Line::finish` (brushed
  on every line). A knob's textured form comes from its tag
  (`setTexturedForm`), then its section's (`ModulePanel::tagTextured`), then
  its drawn size: one-piece at `Tokens::onePieceMaxRadius` or smaller, ringed
  above. **Tag every input, output and volume knob one-piece**; leave the
  rest to size. `ui_layout_tests` fails a trim without its tag and a knob
  that sits on the line or changes form between a module's widths. A line
  a panel draws into the plate goes through `BmoLookAndFeel::fillEngraved`.
  `docs/ui-material-proposal.md` has the design, the costs and the knob
  allocation table.
- **No `juce::Image` with static storage in UI code** -- not a function-local
  static, not a namespace-scope one, not a static container of them. A
  native image holds the graphics framework's shared device objects; held by
  a static it outlives every editor, and releasing it at DLL unload hung the
  host (found 2026-10-02, with the Textured surface on). An image belongs to
  a component, or to `BmoLookAndFeel`'s `MaterialImages`, which every look
  and feel shares through `juce::SharedResourcePointer` and which goes with
  the last editor. `ui_static_image` reads the sources and fails on one.
- Tokens are the only place colours live. A panel that needs a colour
  takes it from `ui::tokens()` or from its module's `accent`.
