#pragma once

#include "core/dsp/Biquad.h"
#include "core/dsp/Prototype.h"
#include "core/dsp/Design.h"
#include "core/dsp/Svf.h"

namespace bmo::deq
{

/** The filter design moved to `core/dsp/` on 2026-09-21, and this is what
    keeps that move invisible to the rest of this module.

    It was module-local here until BMO Defang wanted the same matched-Z design
    -- a de-esser is a narrowband dynamic-EQ cut, so it wants exactly this
    bell, this shelf and this glideable SVF. The repo's rule bars a shared
    *compressor-detector* library ("each dynamics module owns its own
    ReleaseStage/Smoother", docs/1176-comp/00-repo-conventions.md 2), and a
    detector is what Defang copies rather than shares. Filter design is not a
    detector: it is arithmetic with one right answer, the same category as the
    `GainComputer` and `Oversampler` already sitting in `core/dsp/`, and two
    copies of a least-squares zero fit would have to be corrected twice.

    The names are pulled back into `bmo::deq` rather than qualified at every
    site, following `modules/vcomp/dsp/Detector.h`'s `using dsp::Curve`. That
    is also why this file exists at all: the alternative was touching several
    hundred call sites to say `dsp::` in front of `Biquad`, which would have
    buried the one thing the move has to prove -- that DEQ's audio and its
    render are unchanged -- under a diff nobody could read. */

using dsp::kPi;
using dsp::Biquad;

using dsp::Shape;
using dsp::hasGain;
using dsp::Prototype;

using dsp::DesignLimits;
using dsp::DesignGrid;
using dsp::designMatched;
using dsp::matchedPoles;
using dsp::clampFrequency;
using dsp::clampQ;
using dsp::clampGainDb;

using dsp::SvfCoeffs;
using dsp::SvfTaps;
using dsp::SvfState;

} // namespace bmo::deq
