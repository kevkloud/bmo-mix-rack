#pragma once

#include "core/state/ParamLink.h"
#include "core/state/ParamSet.h"
#include "modules/reverb/params.h"

#include <cmath>

namespace bmo::reverb
{

//==============================================================================
/** **A type is a voicing.** Selecting one re-applies that type's ten constants
    over the parameters that hold them -- SIZE, DENSITY, ER SHAPE, ER SPREAD,
    MOD DEPTH, MOD RATE, IN HI-CUT, SOURCE, ER and REVERB -- overwriting
    whatever they currently read. Every time, not only when the module is
    instantiated. Frosty chose that knowingly on 2026-09-21 (11 section 4); the
    alternative, a type that applied its block once and then let the knobs drift
    off it, is a type that tells you less about what you are hearing the longer
    you use it.

    ## The hazard, stated rather than discovered later

    **`type` is an automatable parameter that now writes ten other automatable
    parameters.** Automate TYPE and REVERB together and the two fight: the type
    change stamps a level at the moment the host is driving it somewhere else,
    and which one wins depends on their relative order inside the block. There
    is no arbitration here and there should not be -- an arbiter would have to
    decide that one of the user's two automation lanes is not real. It is
    inherent in "a type is a voicing", it is the cost of the decision, and the
    place to read about it is here and `modules/reverb/AGENTS.md`.

    What *is* guaranteed: the conflict is confined to the ten. A type change
    never touches PRE-DELAY, DECAY, ATTACK, the damping rows, the EQ rows, ER
    MODE, ER HI-CUT, VARIATION, WIDTH, MIX or OUTPUT, so automating any of those
    beside TYPE is safe.

    ## One write path, and it is the preset recall's

    `typeSettings` hands back a `std::vector<Setting>` -- the same type a
    `FactoryPreset` carries -- and this applies it with `ParamSet::apply`, which
    is `setReal` per id, which is `setValueNotifyingHost`. That is the call
    `PresetManager` makes when it loads a factory preset, and the call
    `ParamSet::applyXml` makes for every value in a session. **There is no
    second path.** A type change is a small preset recall, so it cannot come to
    disagree with one about what writing a parameter means, and a host sees the
    same kind of event it already sees when the user picks a preset.

    It also means the two compose in the one order that works. `applyXml` resets
    to defaults and then writes the file's values in spec order, and `type` is
    index 0 -- so a restore applies the stored type's block first and then
    overwrites it with the values the file actually stored. A preset does the
    same because `kType` is first in every one of `factory()`'s lists. **Keep it
    first.** A preset that set kType last would stamp its type's constants over
    its own carefully chosen sizes and levels, and nothing would say so.

    ## Why it cannot recurse, re-enter, or run on the audio thread

    Three things, and the first of them is the structural one:

    - **`type` is not in what a type writes.** `typeSettings` returns ten
      settings and `kType` is not among them, so applying a type can never
      select one. The recursion is not guarded against; it is unconstructible.
    - **`applying` is the belt to that braces.** A host is free to write TYPE
      again from inside one of the ten notifications, and JUCE delivers a
      message-thread change synchronously, so a nested call is reachable even
      though a self-triggered one is not. A nested call returns immediately and
      the outer one finishes the block it started, so the parameters never end
      up carrying half of one type and half of another.
    - **`applied` means only a real move applies.** `setValueNotifyingHost`
      notifies whether or not the value changed, so a preset recall, a state
      restore and `resetToDefaults` all write `type` at least once with nothing
      new in it. Without this, every one of those would stamp a block for no
      reason.

    And the writes reach the parameters **on the message thread**, because
    `juce::ParameterAttachment` marshals a change that arrives on any other one
    through an AsyncUpdater. That matters twice over: automation moves TYPE from
    the audio thread, where `setValueNotifyingHost` has no business being
    called; and an automation ramp across several detents in one block coalesces
    to a single apply of the detent it ended on, rather than to one apply per
    intermediate type.

    ## And it does not fight the smoothers

    Nothing here touches DSP state. Ten parameter values change, `ModuleEngine`
    reads them once at the top of the next block like any other knob move, and
    `DspCore`'s own machinery smooths them -- `kSmoothingMs` for the
    coefficients, `kCrossfadeMs` for SIZE's tap set, the 30 ms raised-cosine dip
    for the table swap TYPE itself causes. A type change is loud, but it is loud
    through exactly the path a hand on the knobs would be, which is the reason
    to write parameters rather than to reach past them. */
class TypeVoicing final : public ParamLink
{
public:
    explicit TypeVoicing (const ParamSet& p)
        : params (p),
          applied (detentOf (p.getReal (Index::type))),
          // Last, because a ParameterAttachment can call back the moment it is
          // built if the parameter moves on another thread, and everything the
          // callback reads has to exist by then.
          attachment (p.param (Index::type), [this] (float v) { typeChanged (v); })
    {
    }

    /** The detent whose block was applied last -- the current voicing, not the
        current parameter, and the two differ only inside an apply. For a test,
        which is the only caller: there is nothing a panel or an engine needs
        from it. */
    int appliedType() const noexcept { return applied; }

private:
    static int detentOf (float value) noexcept
    {
        const auto i = (int) std::lround (value);
        return i >= 0 && i < numTypes ? i : (int) room;
    }

    void typeChanged (float value)
    {
        const auto detent = detentOf (value);

        if (applying || detent == applied)
            return;

        const juce::ScopedValueSetter<bool> guard (applying, true);

        applied = detent;
        params.apply (typeSettings (detent));
    }

    const ParamSet& params;
    int applied;
    bool applying = false;
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TypeVoicing)
};

/** What `ModuleDef::createParamLink` points at. Free rather than a lambda so
    the def can hold a plain function pointer -- see `ParamSpec::textFn` for the
    same reasoning about a static that should not allocate at first use. */
inline std::unique_ptr<ParamLink> createParamLink (const ParamSet& params)
{
    return std::make_unique<TypeVoicing> (params);
}

} // namespace bmo::reverb
