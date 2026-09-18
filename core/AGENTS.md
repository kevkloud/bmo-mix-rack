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
          DSP), SingleModuleProcessor + ProductEditor (a module as a plugin).
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
- Tokens are the only place colours live. A panel that needs a colour
  takes it from `ui::tokens()` or from its module's `accent`.
