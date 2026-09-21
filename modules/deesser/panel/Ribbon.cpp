#include "modules/deesser/panel/Ribbon.h"

#include "core/ui/Fonts.h"
#include "core/ui/Tokens.h"

#include <algorithm>
#include <cmath>

namespace bmo::deesser
{

namespace
{
    /** How much of the past the ribbon shows.

        Three seconds is about a phrase. Shorter and an ess scrolls off before
        the eye has found it; longer and the esses crowd into a texture that
        says "this take is sibilant" without saying which word. */
    constexpr double kSeconds = 3.0;

    /** The floor of the envelope's scale. Below this everything is drawn flat
        on the centre line, which is what silence should look like.

        -60 dBFS rather than -inf: a log scale needs a bottom, and this is the
        band gate's neighbour, so a passage too quiet for the detector to act
        on is also too quiet to draw. */
    constexpr float kFloorDb = -60.0f;

    /** How far back the *unhighlighted* waveform sits. It is context -- what
        was playing -- and the highlight is the point, so the two are separated
        by weight as well as by hue. See the note where it is used. */
    constexpr float kQuietAlpha = 0.45f;

    float normalised (float linear) noexcept
    {
        const auto db = juce::Decibels::gainToDecibels (linear, kFloorDb);
        return juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / -kFloorDb);
    }
}

Ribbon::Ribbon (juce::Colour accentColour) : accent (accentColour)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

Ribbon::~Ribbon()
{
    // Unconditional, and the half of the contract that matters: a tap left
    // enabled by a closed editor costs the audio thread for the rest of the
    // session. BMO DEQ's ResponseView does the same.
    if (source != nullptr)
        source->setEnabled (false);
}

void Ribbon::setTap (AnalyserTap* tap) noexcept
{
    if (source == tap)
        return;

    if (source != nullptr)
        source->setEnabled (false);

    source = tap;

    if (source != nullptr)
    {
        source->setEnabled (true);
        frames.assign ((size_t) std::max (0, source->capacity()), 0.0f);
    }
}

void Ribbon::setRangeDb (float rangeDb)
{
    if (juce::approximatelyEqual (rangeDb, range))
        return;

    range = rangeDb;
    repaint();
}

void Ribbon::timerCallback()
{
    // The tap is the only thing that moves, so there is nothing to compare
    // against to decide whether a repaint is needed -- and at 30 Hz on a strip
    // this size, deciding would cost more than drawing.
    if (source != nullptr)
        repaint();
}

void Ribbon::paint (juce::Graphics& g)
{
    const auto t = ui::panelTokensFor (*this);
    auto area = getLocalBounds().toFloat();
    const auto captionArea = area.removeFromBottom ((float) kCaptionRow);
    const auto bounds = area;

    // The same recess the sketch above sits in, so the two read as one
    // instrument in two panes rather than as two components.
    g.setColour (t.well);
    g.fillRoundedRectangle (bounds, 3.0f);

    const auto plot = bounds.reduced (1.0f);
    const auto centre = plot.getCentreY();

    // The line silence rests on, drawn whether or not there is a tap: an empty
    // ribbon should look like an instrument reading nothing, not like a hole.
    g.setColour (ui::tokens().hairline);
    g.fillRect (juce::Rectangle<float> (plot.getX(), centre, plot.getWidth(),
                                        ui::Tokens::hairlineWeight));

    if (source == nullptr || frames.empty())
        return;

    const auto wanted = (int) (kSeconds * DspCore::kRibbonHz) * DspCore::ribbonFrame;
    const auto read = source->read (frames.data(), std::min (wanted, (int) frames.size()));
    const auto available = read / DspCore::ribbonFrame;

    if (available <= 0)
        return;

    // The two inks the waveform is drawn between. The signal is the module's
    // own accent; what it is acting on is the utility azure, which sits about
    // 165 degrees away in hue and is the one colour in the suite guaranteed
    // clear of every module's accent (core/ui/Tokens.h). Both are resolved
    // against the well rather than used raw, because the well is pale in one
    // appearance and dark in the other.
    const auto quiet  = ui::accentInk (accent, t.well);
    const auto caught = ui::accentInk (ui::tokens().utilGain, t.well);

    // **A pixel column at a time, not three filled paths.**
    //
    // Frosty, 2026-09-21: rather than hang the reduction over the waveform,
    // recolour the stretch of waveform being acted on. It is a better picture
    // -- the notch said "something is happening somewhere up there", and the
    // recolour says "*this* is the bit being caught" -- but it cannot be a
    // path, because the colour now changes *along* the shape rather than
    // between shapes. Drawing by column also puts every frame's peak on
    // screen, so an ess onset cannot fall between two vertices and vanish.
    const auto columns = juce::jmax (1, (int) plot.getWidth());
    const auto perColumn = juce::jmax (1.0f, (float) available / (float) columns);
    const auto half = plot.getHeight() * 0.5f;

    for (int c = 0; c < columns; ++c)
    {
        const auto first = (int) ((float) c * perColumn);
        const auto last = juce::jmin (available, (int) ((float) (c + 1) * perColumn));

        if (first >= last)
            continue;

        auto input = 0.0f, reduction = 0.0f;

        for (int f = first; f < last; ++f)
        {
            const auto at = (size_t) (f * DspCore::ribbonFrame);
            input     = std::max (input, frames[at + DspCore::ribbonInput]);
            reduction = std::max (reduction, frames[at + DspCore::ribbonReduction]);
        }

        const auto height = half * normalised (input);

        if (height <= 0.0f)
            continue;

        // **The recolour is proportional, not a switch.** A hard change of
        // colour at some threshold would draw an edge where the DSP has a
        // slope, and the eye would read the edge of the highlight as the edge
        // of the event. Blending by depth lets the colour say how hard as well
        // as where, which is what the separate reduction layer used to say.
        const auto amount = juce::jlimit (0.0f, 1.0f, reduction / std::max (range, 1.0f));

        // **The highlight brightens as well as changes hue**, and it has to.
        // Resolved against the same well, the accent and the azure land 1.5
        // L* apart and 1.04:1 -- which is separation by hue alone, the exact
        // failure the suite already fixed once on its switch colours and
        // refused once on LTV Comp's bezel pair. Two inks a reader has to tell
        // apart must differ in lightness.
        //
        // Carrying it on the alpha rather than stepping the colour keeps that
        // true in both appearances without a second derivation: the quiet part
        // sits back into whatever the well is, pale or dark, and the caught
        // part comes forward off it.
        g.setColour (quiet.withAlpha (kQuietAlpha)
                          .interpolatedWith (caught.withAlpha (1.0f), amount));
        g.fillRect (juce::Rectangle<float> (plot.getX() + (float) c, centre - height,
                                            1.0f, height * 2.0f));
    }

    drawSuggestion (g, captionArea, caught, available);
}

void Ribbon::drawSuggestion (juce::Graphics& g, juce::Rectangle<float> plot,
                             juce::Colour ink, int available)
{
    // Weighted by how hard the module was working, and **only over the frames
    // it was working on**. Frames where it did nothing carry an estimate of
    // the vowel, and averaging those in would drag the number toward the mids
    // every time somebody stopped saying esses.
    auto weight = 0.0, sum = 0.0;

    for (int f = 0; f < available; ++f)
    {
        const auto at = (size_t) (f * DspCore::ribbonFrame);
        const auto reduction = (double) frames[at + DspCore::ribbonReduction];
        const auto hz = (double) frames[at + DspCore::ribbonPitch];

        if (reduction <= 0.01 || hz <= 0.0)
            continue;

        weight += reduction;
        sum += reduction * std::log (hz);
    }

    // Nothing caught in the last three seconds, so nothing to suggest. The
    // corner stays empty rather than holding the last number: a stale figure
    // under a quiet passage reads as a live one.
    if (weight <= 0.0)
        return;

    const auto hz = std::exp (sum / weight);

    // A tenth of a kHz, which is finer than the knob is ever set and coarse
    // enough not to flicker frame to frame. ASCII only, as the licensed faces
    // require -- so "~" and not an approximation sign, and the tilde is doing
    // real work: this is an estimate from seven filters, not a measurement.
    //
    // Named, because an unlabelled number under a moving picture is read as
    // part of the picture. The suite's rule about a control row being the only
    // thing that names a mode is the same rule.
    //
    // "BITE" rather than "SIBILANCE" -- Frosty, 2026-09-21. It is the module's
    // own word: BMO Defang takes the bite out of a recording, and the number
    // says where that bite is. It is also one syllable against four, which on
    // a 226-px strip is the difference between a caption and a sentence.
    const auto text = "BITE ~" + juce::String (hz / 1000.0, 1) + " kHz";

    g.setFont (ui::captionFont (9.0f));
    g.setColour (ink);
    g.drawText (text, plot, juce::Justification::centred, false);
}

} // namespace bmo::deesser
