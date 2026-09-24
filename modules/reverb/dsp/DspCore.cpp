#include "modules/reverb/dsp/DspCore.h"

// Deliberately almost empty. `bmo_add_module` builds a real static library out
// of a module's DSP sources so that the DSP tests and `measure_reverb` can link
// the engine with no JUCE anywhere near them, and a static library needs at
// least one translation unit -- `DspCore` is a header because what it adds on
// top of its engines is small enough to be one.
//
// The parts that cannot be inline live in their own translation units beside
// it: `ErEngine.cpp` is the early reflections (M2) and `ErTable.cpp` the tables
// they play. `docs/reverb/11-integration-and-test-plan.md` section 1 names the
// late network's -- Fdn.h, Absorbent.h -- each with its own .cpp on the same
// rule.

// It defines nothing of its own since the placeholder tap table was retired
// (2026-09-24): it anchored that table with a one-line function, and the
// library's other translation units now carry its symbols.
