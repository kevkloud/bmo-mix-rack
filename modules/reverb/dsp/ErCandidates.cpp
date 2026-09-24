#include "modules/reverb/dsp/ImageSource.h"

#include <string_view>

// The pinned candidates -- tables for the owner's ear, never for the plugin.
// Compiled into bmo_reverb_ergen only, which no plugin links, so nothing a
// host can reach can select one; erTableFor never returns them. The numbers
// are ErCandidateData.inc, emitted by `measure_reverb taps --emit` beside the
// shipping tables and held equal to the generator by reverb_dsp_tests.

namespace bmo::reverb::ergen
{
namespace
{
    #include "modules/reverb/dsp/ErCandidateData.inc"
}

const ErTable* erCandidateTable (const char* name) noexcept
{
    return (name != nullptr && std::string_view (name) == "cavern-b") ? &kCavernBTable : nullptr;
}

} // namespace bmo::reverb::ergen
