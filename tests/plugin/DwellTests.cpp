/*
    BMO Dwell as a host sees it.

    The golden schema table below is the permanent one: ids 0-19 in the order
    docs/delay/11-integration-and-test-plan.md §3 fixes them, with their ranges,
    defaults and choice counts. A session keys automation by position, so this
    table changing is the schema moving -- a decision for Frosty, not a fix.

    tests/dsp/DwellDspTests.cpp writes the same table out without JUCE, because
    that is the one that runs in the DSP-only CI job. Two copies on purpose: if
    they disagree, one was edited and the other was not.
*/

#include "TestUtil.h"
#include "modules/dwell/presets/FactoryPresets.h"
#include "products/dwell/Product.h"

using namespace test;
namespace P = bmo::dwell;

namespace
{
    const Expected kSchema[]
    {
        { P::kTime,      "Time",         1.0f,  2000.0f,   375.0f,  0 },
        { P::kSync,      "Sync",         0.0f,     1.0f,     0.0f,  2 },
        { P::kNote,      "Note",         0.0f,    15.0f,     8.0f, 16 },
        { P::kFeedback,  "Feedback",     0.0f,   100.0f,    35.0f,  0 },
        { P::kCharacter, "Character",    0.0f,     2.0f,     0.0f,  3 },
        { P::kStereo,    "Stereo",       0.0f,     2.0f,     0.0f,  3 },
        { P::kLowCut,    "Low Cut",     20.0f,  1000.0f,    20.0f,  0 },
        { P::kHighCut,   "High Cut",  1000.0f, 20000.0f, 20000.0f,  0 },
        { P::kVoice,     "Voice",        0.0f,   100.0f,     0.0f,  0 },
        { P::kModRate,   "Mod Rate",     0.1f,     8.0f,     0.6f,  0 },
        { P::kModDepth,  "Mod Depth",    0.0f,   100.0f,     0.0f,  0 },
        { P::kDrive,     "Drive",        0.0f,   100.0f,     0.0f,  0 },
        // DUCK defaults to 0 dB from 2026-09-21: ducking ships inert and
        // opt-in (DECIDED, Frosty; docs/delay/10 §6). Id 12, slot and 0-24 dB
        // range all unchanged -- the default is the only thing that moved.
        { P::kDuck,      "Duck",         0.0f,    24.0f,     0.0f,  0 },
        { P::kMix,       "Mix",          0.0f,   100.0f,    35.0f,  0 },
        { P::kThrow,     "Throw",        0.0f,     1.0f,     0.0f,  2 },
        { P::kThrowMode, "Throw Mode",   0.0f,     2.0f,     0.0f,  3 },
        { P::kFreeze,    "Freeze",       0.0f,     1.0f,     0.0f,  2 },
        { P::kFx,        "FX",           0.0f,     1.0f,     0.0f,  2 },
        { P::kFxType,    "FX Type",      0.0f,     6.0f,     0.0f,  7 },
        { P::kFxAmount,  "FX Amount",    0.0f,   100.0f,    35.0f,  0 },
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using bmo::products::createDwell;

    {
        auto proc = createDwell();
        checkSchema (*proc, kSchema);
        check (P::specs().size() == (size_t) P::Index::count, "the Index enum matches specs()");
        check (P::specs().size() <= 32,
               "every parameter fits a rack slot's host lanes, so all of them are automatable in a rack");
    }

    //== Displayed values ======================================================
    {
        auto proc = createDwell();

        setValue (*proc, P::kTime, 375.0f);
        check (param (*proc, P::kTime).getCurrentValueAsText() == "375 ms", "Time reads in milliseconds");

        setValue (*proc, P::kFeedback, 35.0f);
        check (param (*proc, P::kFeedback).getCurrentValueAsText() == "35 %", "Feedback reads as a percentage");

        setValue (*proc, P::kHighCut, 7300.0f);
        check (param (*proc, P::kHighCut).getCurrentValueAsText() == "7.30 kHz", "High Cut reads in hertz");

        setValue (*proc, P::kDuck, 4.0f);
        check (param (*proc, P::kDuck).getCurrentValueAsText() == "+4.0 dB", "Duck reads in decibels");

        setValue (*proc, P::kNote, 8.0f);
        check (param (*proc, P::kNote).getCurrentValueAsText() == "1/8D", "the default note is the dotted eighth");

        setValue (*proc, P::kCharacter, 2.0f);
        check (param (*proc, P::kCharacter).getCurrentValueAsText() == "Bucket-brigade",
               "the third character names what it is, not what it came from");

        setValue (*proc, P::kFxType, 0.0f);
        check (param (*proc, P::kFxType).getCurrentValueAsText() == "Diffuse",
               "FX type index 0 is the gentlest type, not Off -- fx owns off");
    }

    //== Latency ==============================================================
    // Zero in every configuration, permanently. There is no oversampling and
    // no lookahead, and **the wet delay time is never reported as latency** --
    // a host compensates for a delayed copy of what it sent, not for repeats
    // that are late on purpose. See modules/dwell/dsp/DwellDsp.h.
    {
        auto proc = createDwell();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        proc->prepareToPlay (48000.0, 512);
        check (proc->getLatencySamples() == 0, "no latency at the defaults");

        setValue (*proc, P::kTime, P::kMaxTimeMs);
        setValue (*proc, P::kMix, 100.0f);
        setValue (*proc, P::kFeedback, 100.0f);
        setValue (*proc, P::kDrive, 100.0f);
        setValue (*proc, P::kFx, 1.0f);
        proc->prepareToPlay (48000.0, 512);
        check (proc->getLatencySamples() == 0, "and none at two seconds, wet, with the FX stage on");
    }

    //== State round-trip ======================================================
    {
        juce::MemoryBlock state;

        const bmo::Setting settings[] {
            { P::kTime,       913.0f },
            { P::kSync,         1.0f },
            { P::kNote,         3.0f },
            { P::kFeedback,    61.0f },
            { P::kCharacter,    2.0f },
            { P::kStereo,       1.0f },
            { P::kLowCut,     137.0f },
            { P::kHighCut,   7300.0f },
            { P::kVoice,       44.0f },
            { P::kModRate,      2.7f },
            { P::kModDepth,    29.0f },
            { P::kDrive,       83.0f },
            { P::kDuck,        17.0f },
            { P::kMix,         72.0f },
            { P::kThrow,        1.0f },
            { P::kThrowMode,    2.0f },
            { P::kFreeze,       1.0f },
            { P::kFx,           1.0f },
            { P::kFxType,       5.0f },
            { P::kFxAmount,    12.0f },
        };

        {
            auto proc = createDwell();
            for (const auto& s : settings) setValue (*proc, s.id, s.value);
            proc->getStateInformation (state);
        }

        auto restored = createDwell();
        restored->setStateInformation (state.getData(), (int) state.getSize());

        for (const auto& s : settings)
            checkClose (getValue (*restored, s.id), s.value, 0.01,
                        juce::String ("state round-trip of '") + s.id + "'");
    }

    //== Factory presets ======================================================
    // Init only, and that is the whole list until the module has a sound.
    // docs/delay/14 is where the settings are decided, on a named machine,
    // against the finished loop.
    {
        check (! P::factory().empty(), "there are factory presets");
        check (juce::String (P::factory().front().name) == "Init", "Init is first");
        check (P::factory().front().settings.empty(), "Init is every default");
    }

    return finish ("BMO Dwell");
}
