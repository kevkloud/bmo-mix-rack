#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** The bus contract, in one place, because both products have to state the
    same one: BMO Mix Rack and every standalone module answer
    `isBusesLayoutSupported` from here. It lived as two identical copies until
    mono-in/stereo-out arrived, and that rule is too easy to get half-right in
    one of them.
*/
namespace bmo::buses
{

/** Mono, stereo, and -- only where the caller opts in -- the one conversion
    between them: mono in, stereo out.

    **The conversion is opt-in per module** (`ModuleDef::acceptsMonoInput`,
    Frosty's decision on 2026-09-23). Without it the answer is exactly what
    both processors gave before the conversion existed: mono to mono or stereo
    to stereo, nothing else. The standalone product passes its module's flag;
    the rack passes whether any module it can host has one.

    Stereo in / mono out is *not* accepted by anyone. Folding is a mix decision
    -- which sum, at what gain -- and BMO Util is the module that makes it, on
    the panel, where a user can see it. A processor doing it silently in
    `processBlock` would be a mixer nobody asked for.
*/
inline bool isSupported (const juce::AudioProcessor::BusesLayout& layouts, bool monoInStereoOut)
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (in == out)
        return true;

    return monoInStereoOut
        && in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo();
}

/** Makes the block the modules see, when the output bus is wider than the
    input bus: the input is **duplicated** into the channels it does not cover,
    never cleared.

    Clearing looks like the conservative choice and is the wrong one. A stereo
    buffer whose right channel is silent is not a mono signal, it is a
    hard-left one, and every module in the suite would then be working on a
    signal that no host ever sent it. BMO Dimension is the sharpest case: its
    own guard only catches `numChannels < 2` (see modules/dim/dsp/DspCore.h),
    so on a 1 -> 2 layout it would happily image a hard-left source, the exact
    class of mistake that comment records at +1.17 dB. Every other module would
    put its whole output on the left.

    Duplicating instead hands each module a correlated stereo pair, which is
    precisely what it already gets when a host feeds the same signal to both
    inputs of a stereo instance -- a path that has shipped since 1.0 and that
    every module is already correct for. It is also what a module that
    wants to *generate* stereo needs: the full mono signal is present in both
    channels, so a reverb is free to decorrelate its tail from it. No module's
    DSP has to change for it -- the opt-in in `isSupported` decides only whether
    a host is offered the layout -- and nothing mid-chain in the rack has to
    change width: the rack widens once, here, before slot 1.

    Unity, not -3 dB. A mono track panned centre reaches both speakers at full
    level, and that is the level the module must be given; anything else would
    make a module's own metering and threshold read differently in a layout the
    user did not choose.
*/
inline void spreadInputAcrossOutputs (juce::AudioBuffer<float>& buffer, int numIn, int numOut)
{
    const auto numSamples = buffer.getNumSamples();
    const auto limit      = juce::jmin (numOut, buffer.getNumChannels());

    if (numIn <= 0)
    {
        for (int ch = 0; ch < limit; ++ch)
            buffer.clear (ch, 0, numSamples);

        return;
    }

    for (int ch = numIn; ch < limit; ++ch)
        buffer.copyFrom (ch, 0, buffer, numIn - 1, 0, numSamples);
}

} // namespace bmo::buses
