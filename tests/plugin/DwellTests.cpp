/*
    BMO Dwell as a host sees it.

    The golden schema table below is the permanent one: ids 0-26 in their
    frozen order, with their ranges, defaults and choice counts. A session keys
    automation by position, so this table changing is the schema moving -- a
    decision for Frosty, not a fix.

    tests/dsp/DwellDspTests.cpp writes the same table out without JUCE, because
    that is the one that runs in the DSP-only CI job. Two copies on purpose: if
    they disagree, one was edited and the other was not.
*/

#include "TestUtil.h"
#include "core/rack/RackProcessor.h"
#include "modules/dwell/presets/FactoryPresets.h"
#include "products/dwell/Product.h"

#include <iterator>
#include <string>

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
        { P::kModRate,   "Mod Rate",     0.1f,     8.0f,     0.6f,  0 },
        { P::kModDepth,  "Mod Depth",    0.0f,   100.0f,     0.0f,  0 },
        { P::kDrive,     "Drive",        0.0f,   100.0f,     0.0f,  0 },
        // DUCK defaults to 0 dB from 2026-09-21: ducking ships inert and
        // opt-in (DECIDED, Frosty; docs/delay/10 §6). The 0-24 dB range is
        // unchanged; the default moved then, and the slot moved when VOICE
        // was deleted on the 21st.
        { P::kDuck,      "Duck",         0.0f,    24.0f,     0.0f,  0 },
        { P::kMix,       "Mix",          0.0f,   100.0f,    35.0f,  0 },
        // SEND, the lane's bipolar tail, HOLD and CHOP. `lane_gain` stands
        // where the three-way `throw_mode` did and is a float: below 0 the
        // lane decays, at 0 it holds at unity, above it builds.
        { P::kSend,      "Send",         0.0f,     1.0f,     0.0f,  2 },
        { P::kLaneGain,  "Lane Gain", -100.0f,   100.0f,   -40.0f,  0 },
        { P::kHold,      "Hold",         0.0f,     1.0f,     0.0f,  2 },
        { P::kChop,      "Chop",         0.0f,     1.0f,     0.0f,  2 },
        { P::kFx,        "FX",           0.0f,     1.0f,     0.0f,  2 },
        { P::kFxType,    "FX Type",      0.0f,     2.0f,     0.0f,  3 },
        { P::kFxAmount,  "FX Amount",    0.0f,   100.0f,    35.0f,  0 },
        // The lane, ids 20-25: what it declares for itself once it shares the
        // main delay's voicing. Its gates and its tail are up at 13-16, and
        // the seven rows that used to stand here -- `link` and six `lane_`
        // voicing values -- went on 2026-09-22 (modules/dwell/params.h).
        { P::kLaneLevel,     "Lane Level",      -24.0f,    24.0f,     0.0f,  0 },
        { P::kLaneTime,      "Lane Time",         1.0f,  2000.0f,   250.0f,  0 },
        { P::kLaneNote,      "Lane Note",         0.0f,    15.0f,     6.0f, 16 },
        { P::kLaneFx,        "Lane FX",           0.0f,     1.0f,     0.0f,  2 },
        { P::kLaneFxType,    "Lane FX Type",      0.0f,     2.0f,     0.0f,  3 },
        { P::kLaneFxAmount,  "Lane FX Amount",    0.0f,   100.0f,    35.0f,  0 },
        // Id 26, the last row, and the only parameter in this schema whose
        // default is on.
        { P::kFxLink,        "FX Link",           0.0f,     1.0f,     1.0f,  2 },
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
        check (P::specs().size() == 27, "twenty-seven parameters, ids 0-26");

        //== Every parameter gets a rack automation lane again ================
        //
        // **This assertion said "exactly one row is over the line, and it is
        // `fx_link`" while the schema had thirty-three rows.** Cutting the
        // lane's voicing on 2026-09-22 took it to twenty-six (twenty-seven
        // once `lane_note` joined the same day), so nothing is
        // over the line and the message has to say *that* -- an assertion that
        // went on describing an overflow would pass for the wrong reason the
        // moment the overflow came back.
        //
        // A rack slot has RackProcessor::kParamsPerSlot host lanes.
        // SlotOverflow keeps anything past that working in the panel, the DSP,
        // presets and saved state, and automatable when the plugin runs
        // standalone -- but gives it no lane *in a rack*. What is claimed here
        // is that BMO Dwell needs none of that: every row it has is a row a
        // rack can automate.
        //
        // **Read from the rack rather than hardcoded**, which is the half of
        // the old assertion worth keeping: `kParamsPerSlot` is the rack's
        // number to change, and a test that typed 32 would keep passing while
        // meaning something else if it ever did.
        constexpr auto kLanes = (size_t) bmo::RackProcessor::kParamsPerSlot;

        check (P::specs().size() <= kLanes,
               "every one of BMO Dwell's parameters gets a rack automation lane");

        // Said the other way, because the margin is the point rather than a
        // coincidence: the cut left room to append, so the next parameter can
        // be argued on merit instead of against the ceiling.
        check (kLanes - P::specs().size() == 5,
               "five of a rack slot's lanes are still spare");

        check (std::string (P::specs().back().id) == P::kFxLink,
               "fx_link is the last row -- appended-to-the-end is the only free move after ship");
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

        setValue (*proc, P::kFxType, 2.0f);
        check (param (*proc, P::kFxType).getCurrentValueAsText() == "Crush",
               "the list ends at Crush -- the octaves, Reverse and Sweep are cut");

        // **Zero is the detent LANE GAIN's whole design rests on**: below it
        // the lane decays, above it builds, and at it the lane holds at exact
        // unity. It has to be reachable exactly, which is the 0.1 step
        // dividing a -100..+100 travel evenly about its centre.
        setValue (*proc, P::kLaneGain, 0.0f);
        check (getValue (*proc, P::kLaneGain) == 0.0f, "LANE GAIN's centre detent is exactly zero");

        setValue (*proc, P::kLaneLevel, 0.0f);
        check (param (*proc, P::kLaneLevel).getCurrentValueAsText() == "0.0 dB",
               "Lane Level reads in decibels");
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
            { P::kModRate,      2.7f },
            { P::kModDepth,    29.0f },
            { P::kDrive,       83.0f },
            { P::kDuck,        17.0f },
            { P::kMix,         72.0f },
            { P::kSend,         1.0f },
            { P::kLaneGain,    64.0f },
            { P::kHold,         1.0f },
            { P::kChop,         1.0f },
            { P::kFx,           1.0f },
            { P::kFxType,       2.0f },
            { P::kFxAmount,    12.0f },
            // The lane's own six. LANE NOTE goes to 11 (1/4D), off its 1/8
            // default and off NOTE's 3, so a lane restored from the wrong
            // slot cannot pass.
            { P::kLaneLevel,     -7.5f },
            { P::kLaneTime,     431.0f },
            { P::kLaneNote,      11.0f },
            { P::kLaneFx,         1.0f },
            { P::kLaneFxType,     2.0f },
            { P::kLaneFxAmount,  88.0f },
            // FX LINK goes to 0 because its default is 1: a round trip that
            // restored every default would prove nothing about the one
            // parameter here that does not start at zero-ish.
            { P::kFxLink,         0.0f },
        };

        // Every row, so a parameter appended later and left out here fails
        // instead of going unchecked, as `lane_note` did until 2026-10-01.
        check (std::size (settings) == P::specs().size(),
               "the state round-trip sets every one of BMO Dwell's parameters");

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
