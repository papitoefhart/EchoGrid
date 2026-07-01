#pragma once
#include <JuceHeader.h>

//==============================================================================
// EchoGrid visual theme — DARK dashboard (v0.59): anthracite canvas, dark cards,
// off-white text, pastel accents (lilac = gain, pink = sat/drive, blue = pan/filters,
// green = pitch).  Shared by PluginEditor, NodeTimeline and NodeInspector.
//==============================================================================
namespace eg
{
namespace col
{
    //--- text (light on the dark cards) ---
    const juce::Colour ink   { 0xffe8e5ee };   // all words (off-white)
    const juce::Colour ink2  { 0xffa7a2b1 };   // secondary labels
    const juce::Colour ink3  { 0xff726e7b };   // faint labels / hints

    //--- primary (gain / level) ---
    const juce::Colour lilac     { 0xffc2a4d6 };
    const juce::Colour lilacDeep { 0xffa981c6 };   // emphasis
    const juce::Colour lilacSoft { 0xffdcccea };   // soft fills

    //--- secondary ---
    const juce::Colour pink     { 0xffffafcc };    // sat / drive
    const juce::Colour pinkSoft { 0xffffc8dd };
    const juce::Colour blue     { 0xffa2d2ff };    // pan / filters
    const juce::Colour blueSoft { 0xffbde0fe };
    const juce::Colour green     { 0xff7cc79a };   // pitch (soft green): knob fill + label + deep dot
    const juce::Colour greenSoft { 0xffcdeede };   // pale green: unison dot / soft fills

    //--- surfaces (dark dashboard: cards float a step lighter than the canvas,
    //    controls a step lighter again) ---
    const juce::Colour shadow   { 0xffc2a4d6 };    // (legacy offset block — kept for reference)
    const juce::Colour windowBg { 0xff1e1e1e };    // editor canvas (anthracite — VS Code dark)
    const juce::Colour brand    { 0xfff0eef2 };    // off-white logo text on the dark canvas
    const juce::Colour surface  { 0xff2a2a31 };    // plugin card (elevated off the canvas)
    const juce::Colour panel    { 0xff2a2a31 };    // panel cards (same elevation as surface)
    const juce::Colour raised   { 0xff343039 };    // raised controls: buttons, combos, knob caps
    const juce::Colour line     { 0xff3a3942 };    // subtle borders
    const juce::Colour line2    { 0xff54525e };    // stronger borders / knob track
    const juce::Colour chip     { 0xff232228 };    // readout / chip fills (inset)
    const juce::Colour iconBg   { 0xff343039 };    // section-header icon square
}

//--- shared card geometry ---
inline constexpr float kPanelRadius = 18.0f;

//--- version label shown under the brand logo (bump each build the user ear-tests) ---
inline const juce::String kVersionLabel = "0.71";

//--- soft diffuse drop shadow (dashboard look) — a blurred BLACK shadow below/around a
//    rounded card so it lifts off the dark canvas.  (Was a lilac halo for the old light
//    theme; on the anthracite canvas that read as a purple glow, so it's black now.) ---
inline void drawSoftShadow(juce::Graphics& g, juce::Rectangle<float> r,
                           float radius = kPanelRadius)
{
    juce::Path p;
    p.addRoundedRectangle(r, radius);
    //--- two stacked passes: a soft wide cast + a tighter contact shadow ---
    juce::DropShadow(juce::Colour(0x59000000), 24, { 0, 10 }).drawForPath(g, p);
    juce::DropShadow(juce::Colour(0x66000000),  8, { 0, 4  }).drawForPath(g, p);
}

//--- raised-card border: a 1px stroke lit at the top and shaded at the bottom, so the
//    card reads as a panel catching light from above (pairs with the dark drop-shadow) ---
inline void strokeCardBorder(juce::Graphics& g, juce::Rectangle<float> r,
                             float radius = kPanelRadius)
{
    juce::Path p;
    p.addRoundedRectangle(r, radius);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff54515f), r.getCentreX(), r.getY(),
                                           juce::Colour(0xff232228), r.getCentreX(), r.getBottom(), false));
    g.strokePath(p, juce::PathStrokeType(1.0f));
}

//--- dark rounded card sitting on a soft drop shadow, with a raised top-lit border ---
inline void drawSoftPanel(juce::Graphics& g, juce::Rectangle<float> r,
                          float radius = kPanelRadius)
{
    drawSoftShadow(g, r, radius);
    g.setColour(col::surface);
    g.fillRoundedRectangle(r, radius);
    strokeCardBorder(g, r, radius);
}

//--- small lucide-style "sliders" glyph for section headers (stroked in current colour) ---
inline void drawSlidersIcon(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
{
    g.setColour(c);
    const float x0 = b.getX(), x1 = b.getRight();
    const float rowY[3] = { b.getY() + b.getHeight() * 0.22f,
                            b.getY() + b.getHeight() * 0.50f,
                            b.getY() + b.getHeight() * 0.78f };
    const float knobX[3] = { b.getX() + b.getWidth() * 0.68f,
                             b.getX() + b.getWidth() * 0.34f,
                             b.getX() + b.getWidth() * 0.60f };
    for (int i = 0; i < 3; ++i)
    {
        g.drawLine(x0, rowY[i], x1, rowY[i], 1.3f);
        g.fillEllipse(knobX[i] - 1.7f, rowY[i] - 1.7f, 3.4f, 3.4f);
    }
}
}
