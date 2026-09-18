#pragma once

#include "core/ui/Controls.h"
#include "core/ui/Fonts.h"
#include "core/ui/Tokens.h"

namespace bmo::vcomp
{

//==============================================================================
/** One horizontal bar meter: a caption, a well, and a fill that grows from one
    end.

    **Module-local on purpose.** A horizontal dBFS bar is generic enough to
    belong in core/ui one day, and it will go there the moment a second module
    wants one -- the same rule modules/AGENTS.md applies to DynamicsMeter's
    scale ("the point at which to lift ScalePoint out into the caller -- not
    before"). Lifting it now would mean designing for a caller that does not
    exist. BMO DEQ's ResponseView is the precedent for a panel owning its own
    view.

    **Why this and not DynamicsMeter.** BMO Opto's needle VU is a period
    instrument: it reads average level with VU ballistics on a scale borrowed
    from a 1940s volume indicator, which is right for a module modelling an
    LA-2A and wrong for this one. A modern compressor is judged on peaks in
    dBFS, and the three readings a compressor user actually wants -- what went
    in, what it took off, what came out -- are wanted *at the same time*, which
    a single needle behind a three-way switch cannot do. Three bars show all
    three at once and cost less height than the needle did.

    **Ballistics are the suite's**, taken from OutputMeter: a peak meter jumps
    to a new high immediately and falls back at 0.16 of the distance per tick,
    so it is readable rather than twitchy. Not re-derived here -- if that
    number changes there, change it here. */
class LevelBar final : public juce::Component
{
public:
    /** Which end the fill grows from. Reduction grows leftward from zero on
        the right, which is how every hardware and plugin gain-reduction meter
        has ever read: the bar hangs down from unity rather than building up
        from silence, because that is what the compressor is doing. */
    enum class Grow { rightward, leftward };

    /** `source` returns the reading already in dB -- dBFS for a level, dB of
        reduction for GR. The caller adapts, because the caller is the one that
        knows which of ModuleContext's sources is linear. */
    LevelBar (juce::String caption, Grow, float minDb, float maxDb,
              std::function<float()> source);

    /** One printed figure on the scale: where it sits, in the bar's own dB,
        and what it says.

        The two are separate because on the GR bar they disagree. That bar's
        readings are *amounts of reduction*, 0 to 24 and positive, because that
        is what the DSP hands over -- and what a reader wants printed under it
        is the gain it represents, which is negative. Deriving the text from
        the position would print 24 where the meter means -24. */
    struct ScaleMark
    {
        float db;

        /** Where this reading sits along the bar, 0 at minDb and 1 at maxDb,
            *before* Grow is applied.

            The scale is the mapping as well as the labelling, so the fill, the
            ticks, the figures and the gate handle cannot disagree about where
            a dB is -- there is one list and they all read it. BMO Opto's
            DynamicsMeter::ScalePoint is the same idea and the precedent.

            **It is hand-placed and not a formula.** Frosty asked for -18 at
            the halfway point with the fidelity rising toward 0, and no single
            exponent gives that: the top 18 dB take half the bar, and inside
            that half the spacing still has to open out. Opto's GR scale hit
            the same wall and stopped pretending to be a power law for the same
            reason. The cost is that a value added here is placed by hand and
            its neighbours re-measured; that is cheaper than a formula whose
            comment lies about what it does. */
        float fraction;

        juce::String text;
    };

    /** The printed scale, ends included. Ticks are drawn at the interior marks,
        so the figures and the rules agree by construction rather than by two
        lists being kept in step. */
    void setScale (std::vector<ScaleMark> marks);

    void paint (juce::Graphics&) override;

    /** Pulls a new reading and repaints. Driven from the panel's timer rather
        than one of its own: three bars ticking on three timers would be three
        repaints a frame for one panel. */
    void refresh();

    /** Colour the fill in one colour at every level instead of the suite's
        low/high/clip zones. For the GR bar, where "hot" is not a warning --
        a compressor working hard is not a compressor in trouble, and painting
        24 dB of reduction in the clip red would say it was. */
    void setFlatColour (juce::Colour);

    /** One colour stop along the bar, at a stated reading.

        The fill is a gradient across the *well* rather than one colour chosen
        by the current level, which is what it was until 2026-09-15. The
        difference matters: a zone-coloured bar changes colour along its whole
        length as the level crosses a threshold, so the quiet end of the meter
        turns red when the loud end does. With a gradient a given dB is always
        the same colour, and the bar shows where in its own range it is.

        Stops are in dB and go through `positionOf`, so they follow the scale
        curve with everything else -- the colour at -12 sits exactly where the
        printed -12 does. */
    struct ZoneStop
    {
        float db;
        juce::Colour colour;
    };

    void setZones (std::vector<ZoneStop> stops);

    /** Draw a draggable threshold handle on this bar, bound to `param`.

        This is the gate, and it is the only control on the panel that is not a
        knob or a switch. A gate threshold is the one parameter a user sets by
        looking at the level they are setting it against, so putting it *on*
        the level is the whole point -- a knob would make them read a number
        and translate it into what they can see happening.

        Held as a raw pointer to the parameter, like the attachment classes do;
        the ParamSet outlives every panel. */
    void attachThreshold (juce::RangedAudioParameter&, juce::Colour handleColour);

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** The well, in this component's coordinates -- what the layout test reads
        to check the three bars line up, and what the handle maths uses. */
    juce::Rectangle<int> wellBounds() const;

    static constexpr int kCaptionWidth = 38;
    static constexpr int kBarHeight    = 14;

    /** The strip under the well carrying the printed scale.

        Deep enough that the gate handle, which stands proud of the well on
        both edges, cannot reach the figures. BMO Opto learned this one the
        expensive way: its GR needle rested exactly on the printed 0, in the
        default state of that mode, and the fix was to put the numbers where
        the needle cannot go rather than to give the needle a place to park.
        The default here is the same shape of trap -- the gate rests at -60,
        hard left, on top of the very figure that says so. */
    static constexpr int kScaleRow     = 13;

    /** The strip above the well on a bar that carries a threshold, holding the
        flag and the sliding name over it.

        Reserved by wellBounds on that bar only, and the panel hands that bar a
        box this much taller -- so the three wells stay evenly spaced and the
        block still reads as one instrument. Giving all three the strip would
        cost 40 px of empty plate on the two that have nothing to put in it. */
    static constexpr int kTagRow       = 16;

private:
    /** 0..1 along the well for a dB reading, before Grow is applied. */
    float normalised (float db) const;

    /** 0..1 *across* the well, which is not the same thing: a leftward bar
        reads its minimum at the right-hand end, because that is the end its
        fill grows from.

        The ticks used `normalised` directly until 2026-09-14 and were wrong on
        the GR bar for it -- 0 dB of reduction drawn at the left, where the
        fill starts at the right. Nothing showed, because GR carries one tick
        at 12 of 24 and the midpoint is the single position the two agree on.
        Printing the scale is what made it visible. */
    float positionOf (float db) const;

    /** The inverse of `normalised`: the dB a 0..1 along the bar stands for.
        The mouse needs it, and it has to walk the same marks or a drag and
        the handle disagree wherever the curve bends. */
    float dbAtFraction (float fraction) const;

    /** Sets the threshold parameter from a mouse x, through the standard
        gesture triplet. */
    void setThresholdFromX (int x);

    juce::String caption;
    Grow grow;
    float minDb, maxDb;
    std::function<float()> source;

    juce::Colour flat;
    bool useFlatColour = false;
    std::vector<ScaleMark> scale;
    std::vector<ZoneStop> zones;

    juce::RangedAudioParameter* threshold = nullptr;
    juce::Colour handleColour;
    bool dragging = false;

    float displayed = 0.0f;   ///< linear 0..1 along the well, ballistically smoothed

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelBar)
};

} // namespace bmo::vcomp
