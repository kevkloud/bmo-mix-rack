#include "modules/reverb/dsp/ErTable.h"
#include "modules/reverb/dsp/TapTables.h"

#include <algorithm>
#include <cmath>

// The shipped early-reflection tables. The numbers are not written here: they
// are in ErTableData.inc, which `measure_reverb taps --emit` writes from the
// image-source generator (ImageSource.cpp), and which `reverb_dsp_tests`
// regenerates and holds equal to the generator bit for bit. The generator is
// offline -- it is not compiled into the plugin -- so what ships is the
// numbers, readable in the tree and pinned, and not the machinery.
//
// **A table that fails an audit is re-seeded, not patched** (10 section 8).
// Nothing in the .inc is edited by hand: the recipe's seed changes and the
// file is emitted again.

namespace bmo::reverb
{
namespace
{
    #include "modules/reverb/dsp/ErTableData.inc"

    static_assert (sizeof (kErTables) / sizeof (kErTables[0]) == 6,
                   "one table per type, in the type choice's order");
}

const ErTable& erTableFor (int typeIndex) noexcept
{
    return kErTables[std::clamp (typeIndex, 0, 5)];
}

float erBandCutoffHzAt (const ErTable& table, int band, float sizeM) noexcept
{
    // The generator's kappa (ImageSource.h's kKappa), written again rather
    // than included so the plugin carries no generator header; the pin test
    // fails if the two disagree, because the tables' cutoffs are quoted with it.
    constexpr float kKappa = 0.2f;
    const auto b = band < 0 ? 0 : (band >= kErBands ? kErBands - 1 : band);
    const auto s = sizeM > 0.01f ? sizeM : 0.01f;
    return table.bandCutoffHz[b] * std::pow (kReferenceSizeM / s, kKappa);
}

float erSpanMsAt (const ErTable& table, float sizeM) noexcept
{
    float last = 0.0f;

    for (const auto& v : table.variation)
        for (const auto* ch : { &v.left, &v.right })
            if (ch->numTaps > 0)
                last = std::max (last, ch->taps[ch->numTaps - 1].timeMs);

    return std::min (last * sizeM / kReferenceSizeM, table.windowClampMs);
}

} // namespace bmo::reverb
