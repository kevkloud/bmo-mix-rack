#pragma once

#include "modules/deesser/params.h"
#include <vector>

namespace bmo::deesser
{

/** Five starting points and Init, **named for the source rather than for a
    record, an engineer or a piece of hardware** -- the rule the rest of the
    suite follows, and the reason none of these carries a person's name.

    They are the listening checklist in
    docs/deesser/11-integration-and-test-plan.md 5 turned into settings: a male
    lead vocal, a female one an octave up (01 section 1), an already-bright
    vocal that wants the shelf rather than a notch, spoken dialogue, and
    overheads, where the job is to stay out of the way.

    **There is no level to match, so there is nothing to check.** Every other
    module's presets carry an OUTPUT that matches the preset's loudness to
    Init's, and `tests/plugin/*Tests.cpp` checks it. This module has no output
    trim and cannot have one: a band cut takes under a dB of broadband energy
    (docs/deesser/10-dsp-spec.md 8), and a de-esser that changed the level
    would be a de-esser you had to A/B against a gain. So the level check is
    absent here by construction rather than deferred, which is the opposite of
    a module whose makeup pass has not happened yet.

    **THRESHOLD is in prominence dB, and every figure below is a starting
    point, not a measurement.** `P_ref` -- where 0 prominence-dB sits -- is
    CALIBRATE until it is fitted against real takes (10 section 10.1), and
    fitting it moves what every one of these numbers means. They are set
    relative to each other, which is the part that survives: dialogue asks for
    less than a bright pop vocal, overheads ask for more before they act at
    all. Revisit them with the listening pass, milestone M6. */
inline const std::vector<FactoryPreset>& factory()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", {} },

        // A male lead: the band sits low, 01 section 1's ~3-6 kHz, and a
        // moderate Q keeps it clear of the vowel region underneath.
        { "Male Vocal", { { kFreq, 5200.0f },
                          { kQ, 2.8f },
                          { kThresh, 0.0f },
                          { kRange, 7.0f },
                          { kShape, (float) bell } } },

        // A female lead, the same band an octave higher and a shade narrower:
        // there is less room between the sibilance and the top of the voice.
        { "Female Vocal", { { kFreq, 7600.0f },
                            { kQ, 3.2f },
                            { kThresh, 0.0f },
                            { kRange, 7.0f },
                            { kShape, (float) bell } } },

        // Already bright, and a notch would be heard as a notch. The shelf
        // takes the whole top end down together -- the one shape that dulls,
        // used deliberately and shallowly, with the threshold up so it only
        // acts on the ess itself.
        { "Bright Vocal Shelf", { { kFreq, 6800.0f },
                                  { kQ, 0.9f },
                                  { kThresh, 3.0f },
                                  { kRange, 5.0f },
                                  { kShape, (float) highShelf } } },

        // Speech, close-mic'd: narrow, shallow and early. Dialogue is listened
        // to rather than mixed, so a lisp is more costly here than a surviving
        // ess -- hence the smallest range of the five.
        { "Dialogue", { { kFreq, 6200.0f },
                        { kQ, 4.0f },
                        { kThresh, -2.0f },
                        { kRange, 4.0f },
                        { kShape, (float) bell } } },

        // Cymbals, where the detector has no /s/ to find and false triggering
        // is the whole risk (10 section 10.3). High, narrow, and held well
        // back: this one is meant to do nothing most of the time.
        { "Overheads Safe", { { kFreq, 8800.0f },
                              { kQ, 5.0f },
                              { kThresh, 8.0f },
                              { kRange, 3.0f },
                              { kShape, (float) bell } } },
    };

    return presets;
}

} // namespace bmo::deesser
