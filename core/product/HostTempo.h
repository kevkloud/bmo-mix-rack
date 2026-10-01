#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace bmo
{

/** One block's tempo, as the host gave it: the three values
    `ModuleDsp::setTempo` takes, held together so that `ModuleEngine::process`
    has one argument for them rather than three.

    A default-constructed one is the answer for "no host tempo": no tempo,
    transport not running. That is what a module hears from a host with no
    playhead, and from anything that drives an engine without a host at all.
    `ModuleDsp::setTempo` says what each value means and what is deliberately
    left out. */
struct HostTempo
{
    double bpm   = 0.0;     ///< 0.0 whenever `valid` is false
    bool valid   = false;
    bool playing = false;
};

/** Reads the host's tempo for this block, or the default "no tempo".

    **Both processors call this, once per block, and nothing else reads the
    playhead** -- the rack once for all its slots, which is what keeps two
    synced modules in step. One function for the same reason
    `BusLayouts.h` is one function: the standalone plugin and the rack
    cannot then disagree about what a host meant.

    No playhead, no position, no tempo, or a tempo that is not one (zero,
    negative, not finite) all come back as the same default, `playing`
    included: `ModuleDsp::setTempo` says why the module is not made to tell
    them apart. A stopped transport with a known tempo is valid.

    Called on the audio thread, from `processBlock` and nowhere else, which is
    the one place JUCE says a playhead may be asked. Nothing here allocates,
    locks or touches an atomic of ours -- `getPlayHead` is a load of JUCE's
    own pointer, and the position comes back by value -- so the cost is one
    virtual call into the host per block.

    **0.0 rather than the last tempo seen** when there is none: holding a
    tempo is a policy, and a module that wants it keeps its own last valid
    figure (`ModuleDsp::setTempo`). Remembering one here would put state in
    the plumbing that only one kind of module wants, and would hand a module
    that ignored `valid` a plausible stale tempo instead of an obviously
    absent one. */
inline HostTempo readHostTempo (juce::AudioPlayHead* playHead)
{
    if (playHead == nullptr)
        return {};

    const auto position = playHead->getPosition();

    if (! position.hasValue())
        return {};

    const auto bpm = position->getBpm();

    if (! bpm.hasValue() || ! std::isfinite (*bpm) || *bpm <= 0.0)
        return {};

    return { *bpm, true, position->getIsPlaying() };
}

} // namespace bmo
