#pragma once

#include "core/dsp/AnalyserTap.h"
#include "core/ui/Tokens.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>

namespace bmo::reverb
{

//==============================================================================
/** The spectrum drawn behind the EQ page's response curve.

    **BMO DEQ's `modules/deq/panel/Analyser.h` is the precedent and this is
    deliberately the same instrument**: the same 4096-point Hann FFT, the same
    unnormalised-transform scaling so a full-scale sine reads 0 dBFS, the same
    fast-up/slow-down fold, the same -96 to 0 dBFS well, the same per-pixel-
    column path built on the caller's own `xFor`, and the same "a tenth of a dB
    anywhere" repaint threshold. Every one of those is load-bearing and the
    reasons are written out in DEQ's header; they are not restated here, and a
    change to one of them should be made in both or in neither.

    **It is a copy and not a call, and that is a real cost.** No module in this
    suite includes another module's headers -- checked, 2026-09-21: every
    `#include "modules/..."` in `modules/` names the including module's own
    folder -- and `deq::Analyser` is in `namespace bmo::deq` in DEQ's panel
    folder, so using it here would be the first cross-module dependency in the
    tree and would put DEQ on BMO Linger's link line. The right home for this
    is `core/ui`, which is where the third caller should move it; that is a
    change which edits BMO DEQ, and this pass is not the one to make it. **If
    you are the third caller, promote it rather than copying it again.**

    ## What is not here, and why

    DEQ's `Tint` chooser -- five options with a control on the panel to pick
    between them -- is gone. There is nowhere on a paged 380 px handheld to put
    a five-way chooser that is only meaningful on one of three pages, and DEQ's
    own default settles the question anyway: `Tint::neutral`, Frosty 2026-09-12,
    because it collides with nothing and never competes with the curve in front
    of it. So this draws `analyserNeutral` and takes no option. The module's
    accent is the **curve's** colour, which is the thing a reader is meant to
    be following.

    ## What the audio thread does

    Nothing, until a panel hands over a tap. `setTap` is what enables writing
    and the destructor is what disables it, so a session with no BMO Linger
    window open costs the DSP nothing -- see `core/dsp/AnalyserTap.h`, which is
    emphatic that this path can change neither the sound nor the latency, and
    why a torn read is accepted rather than locked against.

    **And the samples it is showing are the dry input**, because
    `reverb::DspCore` is a marked pass-through and there is no reverb under it
    yet. The tap is at the point the Reverb EQ acts on, which is where it
    belongs once there is one. See `DspCore::eqAnalyser`.
*/
class Spectrum
{
public:
    /** The tap, or null for no spectrum at all. Handing one over starts the
        audio thread writing; handing null back stops it. */
    void setTap (AnalyserTap* t) noexcept
    {
        if (tap == t)
            return;

        if (tap != nullptr)
            tap->setEnabled (false);

        tap = t;

        if (tap != nullptr)
            tap->setEnabled (true);
        else
            magnitudes.fill (kFloorDb);
    }

    ~Spectrum()
    {
        // The other half of setTap's contract: a panel going away has to stop
        // the audio thread writing.
        if (tap != nullptr)
            tap->setEnabled (false);
    }

    /** On whenever there is a tap, and **there is no switch** -- DEQ's rule,
        Frosty 2026-09-15. An analyser you have to find and turn on is one most
        people never see, and the cost argument a toggle would answer is
        already answered by the tap: nothing is written while no editor is up,
        so the off state that matters is free and automatic. */
    bool isEnabled() const noexcept { return tap != nullptr; }

    /** Pulls the newest frame and folds it in. Returns whether anything moved
        enough to be worth a repaint. Message thread. `rate` is the module's
        real sample rate; 0 or less means the host has not prepared it. */
    bool update (double rate)
    {
        if (tap == nullptr || rate <= 0.0)
            return false;

        std::array<float, kFftSize> frame {};

        // Fewer samples than a frame means the audio thread has not filled the
        // tap yet. Draw nothing rather than a frame of zeros, which would read
        // as a real silent signal.
        if (tap->read (frame.data(), kFftSize) < kFftSize)
            return false;

        window.multiplyWithWindowingTable (frame.data(), kFftSize);

        std::array<float, kFftSize * 2> work {};
        std::copy (frame.begin(), frame.end(), work.begin());
        fft.performFrequencyOnlyForwardTransform (work.data());

        const auto scale = 2.0f / (float) kFftSize;

        auto largestMove = 0.0f;

        for (int bin = 0; bin < kBins; ++bin)
        {
            const auto db = juce::Decibels::gainToDecibels (work[(size_t) bin] * scale, kFloorDb);
            auto& shown = magnitudes[(size_t) bin];

            const auto next = db > shown ? db : shown + kFall * (db - shown);

            largestMove = juce::jmax (largestMove, std::abs (next - shown));
            shown = next;
        }

        binHz = rate / (double) kFftSize;
        return largestMove > 0.1f;
    }

    /** The spectrum as a closed shape over `plot`, ready to fill.

        Per pixel column and not per bin: a log frequency axis spreads the low
        bins further apart than a pixel and packs the high ones tighter than
        one, so iterating bins leaves gaps at the bottom and aliases at the
        top. Each column takes the loudest bin that lands in it. `xFor` is the
        caller's own, which is what stops the spectrum drifting from the grid,
        the curve and the node markers it sits behind. */
    template <typename XForHz>
    void buildPath (juce::Path& out, juce::Rectangle<float> plot, XForHz&& xFor) const
    {
        out.clear();

        if (tap == nullptr || binHz <= 0.0)
            return;

        const auto left = plot.getX(), right = plot.getRight();
        const auto columns = juce::jmax (1, (int) std::ceil (right - left));

        auto started = false;
        auto nextBin = 1;   // bin 0 is DC and has no place on a log axis

        for (int c = 0; c <= columns; ++c)
        {
            const auto x = juce::jmin (right, left + (float) c);

            auto loudest = kFloorDb;
            auto any = false;

            while (nextBin < kBins)
            {
                const auto hz = (double) nextBin * binHz;

                if (xFor (hz) > x && any)
                    break;

                if (xFor (hz) > x && ! any)
                {
                    // No bin lands in this column -- it is wider in Hz than
                    // the spacing allows, which happens at the very bottom of
                    // the axis. Take the next one anyway so the shape stays
                    // continuous instead of dropping to the floor.
                    loudest = magnitudes[(size_t) nextBin];
                    any = true;
                    break;
                }

                loudest = juce::jmax (loudest, magnitudes[(size_t) nextBin]);
                any = true;
                ++nextBin;
            }

            if (! any)
                break;

            const auto norm = juce::jlimit (0.0f, 1.0f,
                                            (loudest - kFloorDb) / (kCeilingDb - kFloorDb));
            const auto y = plot.getBottom() - norm * plot.getHeight();

            if (! started)
            {
                out.startNewSubPath (x, plot.getBottom());
                started = true;
            }

            out.lineTo (x, y);
        }

        if (started)
        {
            out.lineTo (right, plot.getBottom());
            out.closeSubPath();
        }
    }

    /** The one colour, and there is no chooser. See the class comment. */
    static juce::Colour colour() { return ui::tokens().analyserNeutral; }

    /** dBFS at the foot and the head of the well, DEQ's figures exactly. */
    static constexpr float kFloorDb = -96.0f, kCeilingDb = 0.0f;

private:
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize  = 1 << kFftOrder;    ///< 4096: 11.7 Hz bins at 48 k
    static constexpr int kBins     = kFftSize / 2;
    static constexpr float kFall   = 0.18f;

    AnalyserTap* tap = nullptr;

    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window
        { (size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann };

    std::array<float, kBins> magnitudes =
        [] { std::array<float, kBins> a {}; a.fill (kFloorDb); return a; }();

    double binHz = 0.0;
};

} // namespace bmo::reverb
