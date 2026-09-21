#include "modules/deesser/panel/Ribbon.h"

#include "core/ui/Tokens.h"

#include <algorithm>
#include <cmath>

namespace bmo::deesser
{

namespace
{
    /** How much of the past the ribbon shows.

        Three seconds is about a phrase. Shorter and an ess scrolls off before
        the eye has found it; longer and the esses crowd together into a texture
        that says "this take is sibilant" without saying which word. */
    constexpr double kSeconds = 3.0;

    /** The floor of the envelope's scale. Below this everything is drawn at
        the centre line, which is what silence should look like.

        -60 dBFS rather than -inf: a log scale needs a bottom, and this one is
        the band gate's neighbour, so a passage too quiet for the detector to
        act on is also too quiet to draw. */
    constexpr float kFloorDb = -60.0f;

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
    const auto bounds = getLocalBounds().toFloat();

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
    const auto count = source->read (frames.data(),
                                     std::min (wanted, (int) frames.size()));

    const auto available = count / DspCore::ribbonFrame;

    if (available <= 0)
        return;

    // Newest at the right, which is the direction every scrolling meter in
    // every DAW moves, and the direction the eye expects time to run.
    const auto width = plot.getWidth();
    const auto perFrame = width / (float) (kSeconds * DspCore::kRibbonHz);
    const auto half = plot.getHeight() * 0.5f;

    // The newest frame sits on the right edge and everything older steps back
    // from it. Written this way round rather than from the left, because the
    // number of frames available grows until the tap fills and a left-anchored
    // drawing would slide the whole picture while it did.
    const auto xFor = [&plot, perFrame, available] (int frame)
    {
        return plot.getRight() - perFrame * (float) (available - 1 - frame);
    };

    // The envelope first, as a silhouette about the centre.
    juce::Path envelope;
    envelope.startNewSubPath (xFor (0), centre);

    for (int i = 0; i < available; ++i)
        envelope.lineTo (xFor (i), centre - half * normalised (frames[(size_t) (i * DspCore::ribbonFrame + DspCore::ribbonInput)]));

    for (int i = available - 1; i >= 0; --i)
        envelope.lineTo (xFor (i), centre + half * normalised (frames[(size_t) (i * DspCore::ribbonFrame + DspCore::ribbonInput)]));

    envelope.closeSubPath();

    g.setColour (ui::tokens().meterQuiet.withAlpha (0.55f));
    g.fillPath (envelope);

    // The band inside it, in the sketch's ink so the two panes are visibly
    // about the same band.
    const auto ink = ui::accentInk (accent, t.well);

    juce::Path bandPath;
    bandPath.startNewSubPath (xFor (0), centre);

    for (int i = 0; i < available; ++i)
        bandPath.lineTo (xFor (i), centre - half * normalised (frames[(size_t) (i * DspCore::ribbonFrame + DspCore::ribbonBand)]));

    for (int i = available - 1; i >= 0; --i)
        bandPath.lineTo (xFor (i), centre + half * normalised (frames[(size_t) (i * DspCore::ribbonFrame + DspCore::ribbonBand)]));

    bandPath.closeSubPath();

    g.setColour (ink.withAlpha (0.85f));
    g.fillPath (bandPath);

    // And the reduction, hanging from the top on RANGE's scale -- the same
    // number the bar below is drawn against, so a full notch here and a full
    // bar there mean the same thing.
    const auto ceiling = std::max (range, 1.0f);

    juce::Path cut;
    cut.startNewSubPath (xFor (0), plot.getY());

    for (int i = 0; i < available; ++i)
    {
        const auto db = frames[(size_t) (i * DspCore::ribbonFrame + DspCore::ribbonReduction)];
        cut.lineTo (xFor (i), plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, db / ceiling));
    }

    cut.lineTo (xFor (available - 1), plot.getY());
    cut.closeSubPath();

    g.setColour (ui::tokens().meterGrWarm.withAlpha (0.75f));
    g.fillPath (cut);
}

} // namespace bmo::deesser
