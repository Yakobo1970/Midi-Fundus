// MIDIARCHIV logo: a single eighth note in front of a hinted table (header row + rows + column lines).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ma
{
inline void drawLogo (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float s = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (s, s);
    const auto bg = juce::Colour (0xff181b24), line = juce::Colour (0xff2c3346), head = juce::Colour (0xff7c9cff);
    g.setColour (bg);
    g.fillRoundedRectangle (r, s * 0.21f);
    if (s >= 40)
    {
        g.setColour (line);
        g.drawRoundedRectangle (r.reduced (s * 0.02f), s * 0.2f, s * 0.012f);
    }

    // table hint
    auto t = r.reduced (s * 0.14f, s * 0.17f);
    if (s >= 24)
    {
        const float rowH = t.getHeight() / 7.0f;
        g.setColour (head.withAlpha (0.55f));
        g.fillRoundedRectangle (t.getX(), t.getY(), t.getWidth(), rowH * 0.8f, rowH * 0.2f);
        for (int i = 1; i < 7; ++i)
        {
            g.setColour (i % 2 ? line : line.brighter (0.15f));
            const float y = t.getY() + i * rowH;
            g.fillRoundedRectangle (t.getX(), y + rowH * 0.12f, t.getWidth(), rowH * 0.62f, rowH * 0.15f);
        }
        g.setColour (bg.withAlpha (0.9f));
        for (float fx : { 0.3f, 0.62f })
            g.fillRect (t.getX() + t.getWidth() * fx, t.getY(), juce::jmax (1.0f, s * 0.018f), t.getHeight());
    }

    // eighth note
    const auto note = juce::Colour (0xfff1f3f9);
    const float cx = r.getX() + s * 0.47f, cy = r.getY() + s * 0.70f;
    const float hw = s * 0.17f, hh = s * 0.125f;
    juce::Path head2;
    head2.addEllipse (-hw, -hh, hw * 2, hh * 2);
    head2.applyTransform (juce::AffineTransform::rotation (-0.42f).translated (cx, cy));
    const float stemX = cx + hw * 0.86f, stemTop = r.getY() + s * 0.2f, stemW = s * 0.055f;
    juce::Path stem;
    stem.addRectangle (stemX - stemW, stemTop, stemW, cy - stemTop - hh * 0.2f);
    juce::Path flag;
    flag.startNewSubPath (stemX - stemW * 0.5f, stemTop);
    flag.cubicTo (stemX + s * 0.02f, stemTop + s * 0.1f, stemX + s * 0.2f, stemTop + s * 0.13f, stemX + s * 0.15f, stemTop + s * 0.33f);
    flag.cubicTo (stemX + s * 0.14f, stemTop + s * 0.22f, stemX + s * 0.06f, stemTop + s * 0.17f, stemX - stemW * 0.5f, stemTop + s * 0.15f);
    flag.closeSubPath();
    juce::Path all;
    all.addPath (head2); all.addPath (stem); all.addPath (flag);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillPath (all, juce::AffineTransform::translation (s * 0.012f, s * 0.018f));
    g.setColour (note);
    g.fillPath (all);
}
} // namespace ma
