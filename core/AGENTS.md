# core/

Shared code. Four layers, each depending only on the ones above it:

```
dsp/      JUCE-free. ModuleDsp (the interface every module's DSP implements),
          Oversampler, Meter. Nothing here includes <juce_*>.
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
  `captureState`, which preset files are also made from. In the rack it rides
  on the module's own carried state, so it follows the module through chain
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
- `SlotParameter::assign` keeps a pointer into the module's static
  `specs()` vector. Never hand it a temporary.
- A slot's `SlotOverflow` is an `AudioProcessor` only so that its
  parameters have an index; JUCE asserts on a gesture without one. Never add
  it to a host, a graph or an editor. It is created and destroyed in
  `rebuild`, after the slot's engine has gone, and a slot's `overflow` member
  is declared before `engine` for the same reason.
- Anything that changes a `ModuleEngine` in the rack goes through
  `RackProcessor::rebuild`, which calls `rackChainWillChange` before and
  `rackChainChanged` after, synchronously, so the editor drops its panels
  before their engines die. Keep it that way.
- `processBlock` in the rack takes a `ScopedTryLock` and passes audio
  through if the message thread is mid-rebuild. Never block the audio
  thread on the chain lock.
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
  remembers a tempo. **What a module must not assume:** that a held tempo
  survives a chain edit (`rebuild` gives every slot a new engine, touched
  or not, and the new one is handed the tempo again on its first block --
  which matters only while the host's tempo is invalid); or that `prepare`
  comes with a tempo (the first `setTempo` comes with the first block).
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
