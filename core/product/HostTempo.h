#pragma once

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

} // namespace bmo
