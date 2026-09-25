#include "ModulePanel.h"
#include "Line.h"
#include "LookAndFeel.h"
#include "core/product/ModuleDef.h"

namespace bmo::ui
{

// Out of line, and this file exists only for that. ModuleDef.h includes
// ModulePanel.h, so the header can only forward-declare ModuleDef -- which is
// enough to hold a reference to one and not enough to read `def.accent` off
// it. The three panels that used to paint their own rules each included
// ModuleDef.h and so never met this.
void ModulePanel::paintRules (juce::Graphics& g) const
{
    const auto t = panelTokens();

    for (const auto& r : rules)
    {
        if (r.text.isEmpty())
            drawRule (g, r.row, r.span);
        else
            drawRuleLegend (g, r.row, r.text,
                            inkFor (context.def.lineOf()).value_or (context.def.accent),
                            t.plate, r.span);
    }
}

bool ModulePanel::materialPlate() { return BmoLookAndFeel::materialEnabled(); }

Tokens ModulePanel::panelTokens() const
{
    return groundFor (context.def.lineOf());
}

void ModulePanel::paint (juce::Graphics& g)
{
    const auto plate = panelTokens().plate;

    if (! BmoLookAndFeel::materialEnabled()
        || juce::SystemStats::getEnvironmentVariable ("BMO_MATERIAL", {}).contains ("noplate"))
    {
        g.fillAll (plate);
    }
    else
    {
        // The pixel scale the graphics context really renders at: the
        // editor's own scale transform times the display's. Caching at design
        // size and letting the transform stretch it would blur the grain.
        const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto w = juce::roundToInt ((float) getWidth()  * scale);
        const auto h = juce::roundToInt ((float) getHeight() * scale);

        if (plateCache.isNull() || plateCache.getWidth() != w || plateCache.getHeight() != h
            || ! juce::approximatelyEqual (plateCacheScale, scale) || plateCacheColour != plate)
        {
            plateCache = juce::Image (juce::Image::RGB, juce::jmax (1, w), juce::jmax (1, h), false);
            juce::Graphics cg (plateCache);
            cg.addTransform (juce::AffineTransform::scale (scale));
            cg.fillAll (plate);
            BmoLookAndFeel::paintPlateMaterial (cg, getLocalBounds());
            plateCacheScale = scale;
            plateCacheColour = plate;
        }

        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (plateCache, getLocalBounds().toFloat());
    }

    paintRules (g);
    paintPanel (g);
}

Tokens panelTokensFor (const juce::Component& c)
{
    // Walked rather than plumbed. A control that draws a *ground* -- a meter
    // trough, a pressed button -- needs its panel's plate, and there are two
    // ways to give it one: pass a palette down through every constructor, or
    // let it ask. Passing it down grows a parameter on every control in the
    // suite for the sake of the few that draw a recess, and means a theme
    // change has to be pushed to all of them rather than simply repainted.
    //
    // A rack is why this cannot be a global "current line" instead. JUCE
    // paints each component in its own paint() call rather than inside its
    // parent's, so a scope opened in ModulePanel::paint would be closed again
    // long before any child painted -- and a rack holds panels of different
    // lines side by side, so the answer genuinely differs per component.
    //
    // Null until a control is added to a panel, which is why it falls back:
    // constructors run before parenting, and a look-and-feel may be asked to
    // paint a control that is in no panel at all.
    return groundFor (panelLineFor (c));
}

const Line& panelLineFor (const juce::Component& c)
{
    if (auto* panel = c.findParentComponentOfClass<ModulePanel>())
        return panel->getContext().def.lineOf();

    return bmoLine();
}

juce::Colour panelAccentFor (const juce::Component& c, juce::Colour fallback)
{
    if (const auto ink = inkFor (panelLineFor (c)))
        return *ink;

    return fallback;
}

} // namespace bmo::ui
