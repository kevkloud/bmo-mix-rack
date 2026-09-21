// Everything is inline in the header; this translation unit exists so
// bmo_add_module has a source file to build the DSP-only static library
// from, the same shape every other module's CMakeLists entry expects.
//
// The DSP pass will have real code to put here -- the filter design, the
// detector and the gain computer are not header material -- so this file is
// where that lands rather than a second one appearing beside it.
#include "DspCore.h"
