#include "modules/reverb/dsp/DspCore.h"

// Deliberately almost empty. `bmo_add_module` builds a real static library out
// of a module's DSP sources so that the DSP tests and `measure_reverb` can link
// the engine with no JUCE anywhere near them, and a static library needs at
// least one translation unit -- `DspCore` is a header today because the
// placeholder is small enough to be one.
//
// When the engine lands this file is where the parts that cannot be inline go:
// the image-source tap generation, the Householder matrix, the absorbent filter
// design. `docs/reverb/11-integration-and-test-plan.md` section 1 names the
// other headers it grows -- ErGenerator.h, Fdn.h, Absorbent.h -- each with its
// own .cpp on the same rule.

namespace bmo::reverb
{

// One definition so the translation unit is not empty and the tap table is
// linked rather than being discarded as unused inline data.
const Tap& firstReferenceTap() noexcept { return kReferenceTaps[0]; }

} // namespace bmo::reverb
