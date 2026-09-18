#include "ResponseView.h"
#include "core/ui/LookAndFeel.h"
#include "core/ui/Tokens.h"
#include "modules/deq/dsp/DeqDsp.h"
#include "modules/deq/panel/Widgets.h"
#include "modules/deq/panel/Widgets.h"

namespace bmo::deq
{

namespace
{
    constexpr double kLowHz = 20.0, kHighHz = 20000.0;

    // Mockups A and C (2026-09-11): 8 px nodes numbered on the full panel,
    // 4 px and bare on the compact one; the selected node 2 px bigger again.
    constexpr float kNodeRadius = 8.0f, kNodeRadiusCompact = 4.0f;
}

ResponseView::ResponseView (ParamSet& p, juce::Colour a, std::function<int()> sel, std::function<void (int)> choose,
                            std::function<double()> rate)
    : params (p), accent (a), selected (std::move (sel)), select (std::move (choose)), hostRate (std::move (rate))
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    readBands();
    startTimerHz (30);
}

ResponseView::~ResponseView() = default;

//==============================================================================
juce::Rectangle<float> ResponseView::plot() const
{
    // Room under the plot for the frequency axis, and on the other three sides
    // for a node at the end of its range to hang over the well's edge rather
    // than be cut by it (kOverhang).
    //
    // The top inset is the half of that which was missing until 2026-09-15.
    // Sideways it always worked -- it is why band 12's node at 18 kHz is whole
    // -- but a band at **+24 dB** put its node's centre on the component's own
    // top edge, so JUCE cut the top half off and took half the band number with
    // it. The panel places this component 8 px higher to pay for the inset, so
    // the plot itself does not move and nothing else on the panel does either.
    return getLocalBounds().toFloat()
             .reduced ((float) kOverhang, 0.0f)
             .withTrimmedTop ((float) kOverhang)
             .withTrimmedBottom (compact ? 16.0f : 18.0f);
}

float ResponseView::xFor (double hz) const
{
    const auto r = plot();
    return r.getX() + r.getWidth() * (float) (std::log (hz / kLowHz) / std::log (kHighHz / kLowHz));
}

double ResponseView::hzFor (float x) const
{
    const auto r = plot();
    const auto n = juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth()));
    return kLowHz * std::pow (kHighHz / kLowHz, n);
}

float ResponseView::yFor (double db) const
{
    const auto r = plot();
    return r.getCentreY() - (float) (db / kSpanDb) * r.getHeight() * 0.5f;
}

double ResponseView::dbFor (float y) const
{
    const auto r = plot();
    return juce::jlimit (-(double) kSpanDb, (double) kSpanDb, (double) ((r.getCentreY() - y) / (r.getHeight() * 0.5f) * kSpanDb));
}

juce::Point<float> ResponseView::nodeFor (const Band& b) const
{
    const auto cut = b.shape == Shape::lowCut || b.shape == Shape::highCut;
    return { xFor (b.hz), yFor (cut ? 0.0 : b.gainDb) };
}

//==============================================================================
bool ResponseView::readBands()
{
    auto changed = false;

    for (int i = 0; i < kBands; ++i)
    {
        Band b;
        b.on        = value (i, Control::on) > 0.5f && params.getReal (kActive) > 0.5f;
        b.dynamic   = value (i, Control::dyn) > 0.5f;
        const auto shapeChoice = (int) std::lround (value (i, Control::shape));
        b.shape     = DeqDsp::shapeFor (shapeChoice);
        b.placement = DeqDsp::placementFor ((int) std::lround (value (i, Control::place)));
        b.hz        = value (i, Control::freq);
        b.q         = effectiveQ (shapeChoice, value (i, Control::q));   // what the engine runs
        b.gainDb    = value (i, Control::gain);
        b.rangeDb   = value (i, Control::range);

        auto& was = bands[(size_t) i];
        if (b.on != was.on || b.dynamic != was.dynamic || b.shape != was.shape || b.placement != was.placement
            || b.hz != was.hz || b.q != was.q || b.gainDb != was.gainDb || b.rangeDb != was.rangeDb)
        {
            was = b;
            changed = true;
        }
    }

    return changed;
}

void ResponseView::rebuildPaths()
{
    const auto r = plot();
    std::array<Biquad, kBands> designs;

    for (int i = 0; i < kBands; ++i)
        designs[(size_t) i] = designMatched (bands[(size_t) i].shape, bands[(size_t) i].hz, bands[(size_t) i].q,
                                             bands[(size_t) i].gainDb, grid);

    const auto sel = selected ? selected() : -1;
    curve.clear();
    selectedFill.clear();

    const auto points = juce::jmax (64, (int) r.getWidth());

    for (int k = 0; k <= points; ++k)
    {
        const auto x  = r.getX() + r.getWidth() * (float) k / (float) points;
        const auto hz = hzFor (x);
        const auto w  = 2.0 * kPi * hz / drawnAt;

        std::complex<double> h = 1.0;
        for (int i = 0; i < kBands; ++i)
        {
            const auto& b = bands[(size_t) i];
            if (b.on && b.placement != Placement::side)
                h *= designs[(size_t) i].responseAt (w);
        }

        const auto y = yFor (juce::jlimit (-(double) kSpanDb * 1.2, (double) kSpanDb * 1.2, 20.0 * std::log10 (std::max (std::abs (h), 1.0e-9))));
        k == 0 ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);

        // The selected band's own shape, shaded under the curve -- but only if
        // it is on. Shading the shape of a band that is off drew a low cut
        // across the default panel that nothing was doing.
        if (sel >= 0 && sel < kBands && bands[(size_t) sel].on)
        {
            const auto own = 20.0 * std::log10 (std::max (std::abs (designs[(size_t) sel].responseAt (w)), 1.0e-9));
            const auto yo = yFor (juce::jlimit (-(double) kSpanDb * 1.2, (double) kSpanDb * 1.2, own));
            k == 0 ? selectedFill.startNewSubPath (x, yo) : selectedFill.lineTo (x, yo);
        }
    }

    if (! selectedFill.isEmpty())
    {
        selectedFill.lineTo (r.getRight(), yFor (0.0));
        selectedFill.lineTo (r.getX(), yFor (0.0));
        selectedFill.closeSubPath();
    }
}

void ResponseView::timerCallback()
{
    const auto sel = selected ? selected() : -1;

    // The rate the module is actually running at. Polled rather than read once
    // in the constructor, because a host can re-prepare a plugin while its
    // editor is open -- change the device rate, or render offline at 96 k with
    // the window up -- and the curve has to follow it there. 0 means "not
    // prepared yet" and keeps the design rate rather than dividing by it.
    auto rateMoved = false;

    if (hostRate != nullptr)
    {
        const auto now = hostRate();

        if (now > 0.0 && now != drawnAt)
        {
            drawnAt = now;
            grid = DesignGrid::make (drawnAt);
            rateMoved = true;
        }
    }

    // The spectrum moves on its own, every frame, independently of whether a
    // parameter did -- so it gets its own rebuild rather than riding on the
    // curve's. `update` says whether the shape actually changed, so a settled
    // or silent analyser costs a read and a comparison and no repaint.
    const auto spectrumMoved = analyser.update (drawnAt);

    if (spectrumMoved)
        rebuildSpectrum();

    if (readBands() || sel != lastSelected || rateMoved)
    {
        lastSelected = sel;
        rebuildPaths();
        repaint();
    }
    else if (spectrumMoved)
    {
        repaint();
    }
}

void ResponseView::rebuildSpectrum()
{
    analyser.buildPath (spectrum, plot(), [this] (double hz) { return xFor (hz); });
}

void ResponseView::resized()
{
    rebuildPaths();
    rebuildSpectrum();   // its shape is in the plot's coordinates, so it moves too
}

//==============================================================================
void ResponseView::paint (juce::Graphics& g)
{
    const auto& t = ui::tokens();
    const auto r = plot();

    g.setColour (t.well);
    g.fillRoundedRectangle (r, ui::Tokens::corner);

    // The spectrum goes in first, under the grid and well under the curve.
    //
    // It measures a different quantity from everything drawn over it -- signal
    // level, not EQ gain -- so it is deliberately the quietest thing in the
    // well: a filled shape at low alpha with no outline, which reads as
    // texture behind a scale rather than as a second line to be compared with
    // the first. Alpha rather than a pre-dimmed colour, so a theme that
    // overrides the token still lands where the author meant it to.
    if (analyser.isEnabled() && ! spectrum.isEmpty())
    {
        g.saveState();
        g.reduceClipRegion (r.getSmallestIntegerContainer());
        g.setColour (analyser.colourFor (accent).withAlpha (0.28f));
        g.fillPath (spectrum);
        g.restoreState();
    }

    // Grid: quiet, under everything. The full panel has decades and their
    // halves and every 6 dB; the compact one only the decades and +-12, as
    // mockup C drew it -- at 300 px any more is texture, not a scale.
    g.setColour (t.plateEdge);
    const auto gridHz = compact ? std::vector<double> { 100.0, 1000.0, 10000.0 }
                                : std::vector<double> { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 };
    const auto gridDb = compact ? std::vector<double> { -12.0, 12.0 }
                                : std::vector<double> { -18.0, -12.0, -6.0, 6.0, 12.0, 18.0 };
    for (auto hz : gridHz)
        g.fillRect (juce::Rectangle<float> (xFor (hz), r.getY(), 1.0f, r.getHeight()));
    for (auto db : gridDb)
        g.fillRect (juce::Rectangle<float> (r.getX(), yFor (db), r.getWidth(), 1.0f));
    g.setColour (t.hairline);
    g.fillRect (juce::Rectangle<float> (r.getX(), yFor (0.0), r.getWidth(), 1.0f));

    const auto axis = ui::captionFont (10.0f);

    // The gain scale, on the full panel only, inside the well at its left edge.
    if (! compact)
        for (const auto& [db, text] : std::vector<std::pair<double, const char*>> { { 12.0, "+12" }, { 0.0, "0" }, { -12.0, "-12" } })
            ui::drawLabel (g, text, { r.getX() + 4.0f, yFor (db) - 13.0f, 40.0f, 12.0f },
                           juce::Justification::bottomLeft, axis, t.text2);

    const auto labels = compact ? std::vector<std::pair<double, const char*>> { { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } }
                                : std::vector<std::pair<double, const char*>> { { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" },
                                                                                 { 1000.0, "1k" }, { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" } };
    // The frequency labels are drawn last, under the nodes -- see the foot of
    // this function.

    g.saveState();
    g.reduceClipRegion (r.toNearestInt());

    g.setColour (accent.withAlpha (0.16f));
    g.fillPath (selectedFill);

    g.setColour (accent);
    g.strokePath (curve, juce::PathStrokeType (compact ? 1.6f : 2.0f, juce::PathStrokeType::curved));

    // Dynamic ranges first, so the nodes sit on top of their whiskers.
    for (const auto& b : bands)
        if (b.on && b.dynamic && b.shape != Shape::lowCut && b.shape != Shape::highCut)
        {
            const auto from = nodeFor (b);
            const auto to = yFor (juce::jlimit (-(double) kSpanDb, (double) kSpanDb, b.gainDb + b.rangeDb));
            g.setColour (t.meterGr);
            g.drawLine (from.x, from.y, from.x, to, 2.0f);
            g.drawLine (from.x - 4.0f, to, from.x + 4.0f, to, 2.0f);
        }

    // Nodes over the well's edge rather than cut by it: band 12's default
    // 18 kHz sits 7 px from the right-hand side, and a node is 8 px across.
    g.restoreState();

    const auto sel = selected ? selected() : -1;
    const auto numberFont = ui::labelFont (9.0f, true);

    for (int i = 0; i < kBands; ++i)
    {
        const auto& b = bands[(size_t) i];

        // Only a band that is on gets a node. Twelve of them over a flat
        // curve, eleven of which do nothing, read as a control surface rather
        // than as the EQ's state, and there is no way to tell by eye which one
        // is live (Frosty, 2026-09-11). A band that is off but selected keeps
        // its node, dimmed: the lane strip can select one, and a selection
        // with nothing on screen is worse than a quiet dot. Double-clicking
        // the plot still switches the first free band on, which is how a band
        // is added now that there is nothing to drag.
        if (! b.on && i != sel)
            continue;

        const auto at = nodeFor (b);
        const auto isSel = i == sel;

        // The band's own colour, from its placement: a node says whether it is
        // Stereo, Mid or Side without being selected and without a legend.
        const auto mine = placementColour ((int) b.placement, accent);
        const auto radius = compact ? kNodeRadiusCompact : kNodeRadius;
        const auto node = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (at);

        if (isSel)
        {
            const auto big = node.expanded (compact ? 1.5f : 2.0f);

            if (b.on)
            {
                g.setColour (mine);
                g.fillEllipse (big);
                g.setColour (ui::accentInk (mine));
                g.drawEllipse (big, 1.6f);
            }
            else
            {
                // Selected and off: there, and obviously inert.
                g.setColour (t.well);
                g.fillEllipse (big);
                g.setColour (mine.withAlpha (0.45f));
                g.drawEllipse (big, 1.6f);
            }
        }
        else
        {
            g.setColour (t.well);
            g.fillEllipse (node);
            g.setColour (b.on ? mine : t.hairline);
            g.drawEllipse (node, compact ? 1.4f : 1.6f);
        }

        if (! compact)
            ui::drawLabel (g, juce::String (i + 1), node.expanded (2.0f), juce::Justification::centred,
                           isSel ? ui::labelFont (10.0f, true) : numberFont,
                           isSel && b.on ? ui::onAccentOf (mine) : (b.on ? t.text1 : t.text2));
    }

    // The frequency scale goes on last, over any node that has hung down into
    // it.
    //
    // A band at **-24 dB** puts its node's centre on the plot's bottom edge,
    // and the clip is deliberately off by now so that nodes ride over the
    // well's edge instead of being cut by it. Drawn before the nodes, as they
    // were until 2026-09-15, that put a node squarely on top of "100" and took
    // the scale with it. Drawing the numbers afterwards keeps both: the node
    // stays whole and where its gain says it is, and the scale stays readable
    // because it is in front.
    for (const auto& [hz, text] : labels)
        // 48 px boxes: the caption face is wide, and a 32 px box cut "100" to
        // "10" on the first render.
        ui::drawLabel (g, text, { xFor (hz) - 24.0f, r.getBottom() + 2.0f, 48.0f, 13.0f },
                       juce::Justification::centred, axis, t.text2);

    // No readout over the curve: the knobs carry their own numbers now
    // (PlainKnob::setShowsValue), which is where the mockups put them.
}

//==============================================================================
int ResponseView::bandAt (juce::Point<float> p) const
{
    // The selected band wins a tie, then the nearest -- nodes can sit on top of
    // each other, and the one being worked on is the one wanted.
    const auto sel = selected ? selected() : -1;
    const auto reach = compact ? 9.0f : 11.0f;
    int best = -1;
    float bestDistance = reach;

    for (int i = 0; i < kBands; ++i)
    {
        // A band with no node cannot be grabbed: reaching one that is off used
        // to move its frequency and gain with nothing to see and nothing to
        // hear. The selected band is reachable whether or not it is on,
        // because it is the one drawn.
        if (! bands[(size_t) i].on && i != sel)
            continue;

        const auto d = nodeFor (bands[(size_t) i]).getDistanceFrom (p);
        if (i == sel && d <= reach)
            return i;
        if (d < bestDistance)
        {
            best = i;
            bestDistance = d;
        }
    }

    return best;
}

void ResponseView::setSolo (int band)
{
    if (band == soloing)
        return;

    soloing = band;

    if (onSolo)
        onSolo (band);
}

void ResponseView::mouseDown (const juce::MouseEvent& e)
{
    // `bands` is a *drawing* cache, refreshed on a 30 Hz timer, and hit-testing
    // has always read it. A mouse arrives asynchronously to that timer, so a
    // band switched on by automation, a preset or the tab strip in the last
    // 33 ms was not grabbable and its node could be clicked straight through.
    // Cheap to rule out -- twelve bands of eight parameters -- and it makes a
    // click resolve against what is true rather than what was last painted.
    readBands();

    // Right-click held is solo, the same gesture the tabs carry. It does not
    // start a drag and does not change the selection: auditioning a band is not
    // the same as going to work on it.
    if (e.mods.isPopupMenu())
    {
        if (const auto hit = bandAt (e.position); hit >= 0 && bands[(size_t) hit].on)
            setSolo (hit);

        return;
    }

    dragging = bandAt (e.position);

    if (dragging < 0)
        return;

    if (select)
        select (dragging);

    params.param (indexOf (dragging, Control::freq)).beginChangeGesture();
    params.param (indexOf (dragging, Control::gain)).beginChangeGesture();
}

void ResponseView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;

    // **Solo while dragging**, which is the whole reason this lives here rather
    // than in mouseDown.
    //
    // JUCE does not deliver a second mouseDown while a button is already held
    // -- `MouseInputSourceImpl::setButtons` says so in as many words, "ignore
    // secondary clicks when there's already a button down", and returns before
    // sending anything. What it *does* do is update the button state and send a
    // drag, so the right button appears in this event's modifiers and nowhere
    // else. Handling it here is the only place the gesture exists.
    //
    // `isRightButtonDown` rather than `isPopupMenu`: on macOS the latter is
    // also ctrl-click, and ctrl is a modifier somebody may well be holding for
    // a fine drag.
    if (e.mods.isRightButtonDown())
        setSolo (dragging);
    else if (soloing >= 0)
        setSolo (-1);

    const auto x = juce::jlimit (plot().getX(), plot().getRight(), e.position.x);
    params.setReal (indexOf (dragging, Control::freq), (float) hzFor (x));

    const auto& b = bands[(size_t) dragging];
    if (b.shape != Shape::lowCut && b.shape != Shape::highCut)
        params.setReal (indexOf (dragging, Control::gain), (float) dbFor (e.position.y));
}

void ResponseView::mouseUp (const juce::MouseEvent&)
{
    // Unconditional, and before the drag check: a solo begun by right-clicking
    // a node with no drag never set `dragging`, and would otherwise never be
    // released.
    setSolo (-1);

    if (dragging < 0)
        return;

    params.param (indexOf (dragging, Control::freq)).endChangeGesture();
    params.param (indexOf (dragging, Control::gain)).endChangeGesture();
    dragging = -1;
}

void ResponseView::mouseDoubleClick (const juce::MouseEvent& e)
{
    readBands();   // same reason as mouseDown: hit-test against what is true

    auto set = [this] (int band, Control c, float v)
    {
        auto& p = params.param (indexOf (band, c));
        p.beginChangeGesture();
        params.setReal (indexOf (band, c), v);
        p.endChangeGesture();
    };

    // On a node: switch that band off. Any node that is on answers, not only
    // the selected one (Frosty, 2026-09-15) -- what you double-click is what
    // happens, and `bandAt` already gives the selected band the tie when two
    // nodes overlap.
    //
    // A band that is off but selected still draws a node, and that one does
    // nothing here: switching it off again would be a gesture with no effect,
    // and switching it *on* would make one double-click mean both things
    // depending on state. Its tab is the way back on.
    if (const auto hit = bandAt (e.position); hit >= 0)
    {
        if (value (hit, Control::on) > 0.5f)
            set (hit, Control::on, 0.0f);

        return;
    }

    // The first band that is off, switched on as a bell where the click was.
    for (int i = 0; i < kBands; ++i)
        if (value (i, Control::on) < 0.5f)
        {
            set (i, Control::shape, 0.0f);
            set (i, Control::freq, (float) hzFor (e.position.x));
            set (i, Control::gain, (float) dbFor (e.position.y));
            set (i, Control::on, 1.0f);

            if (select)
                select (i);
            return;
        }
}

void ResponseView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto sel = selected ? selected() : -1;
    if (sel < 0)
        return;

    const auto index = indexOf (sel, Control::q);
    auto& p = params.param (index);

    // From the Q the band is running at, and no further than a shelf goes, so
    // a wheel on a shelf never winds the knob up past what it can do.
    const auto shapeChoice = (int) std::lround (value (sel, Control::shape));
    const auto q = effectiveQ (shapeChoice, effectiveQ (shapeChoice, params.getReal (index)) * std::pow (1.15f, wheel.deltaY * 4.0f));

    p.beginChangeGesture();
    params.setReal (index, q);
    p.endChangeGesture();
}

} // namespace bmo::deq
