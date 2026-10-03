/*
    BMO Dwell's DSP core is header-only while it is still plumbing. This
    translation unit exists so `bmo_dwell_dsp` is a real static library like
    every other module's, which is what the DSP tests and the measurement
    harness link against -- and so that stage 2 has somewhere to put the ring,
    the character chain and the FX stage without moving files under a branch
    that is already in review.
*/

#include "modules/dwell/dsp/DspCore.h"

namespace bmo::dwell
{

// Nothing out of line yet. docs/delay/10-dsp-spec.md says what goes here.

} // namespace bmo::dwell
