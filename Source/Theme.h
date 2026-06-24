#pragma once
#include <JuceHeader.h>

//==============================================================================
// EchoGrid visual theme — the "modern / calm" pastel palette (v0.25 overhaul).
// Lilac primary, pink = Sat/Drive, blue = Pan/filters, anthracite for all text.
// Shared by PluginEditor, NodeTimeline and NodeInspector so colours stay in sync.
//==============================================================================
namespace eg
{
namespace col
{
    //--- text (anthracite) ---
    const juce::Colour ink   { 0xff33333a };   // all words
    const juce::Colour ink2  { 0xff87838d };   // secondary labels
    const juce::Colour ink3  { 0xffb4afba };   // faint labels / hints

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

    //--- surfaces ---
    const juce::Colour shadow   { 0xffc2a4d6 };    // (legacy offset block — kept for reference)
    const juce::Colour windowBg { 0xfff3edf6 };    // editor background
    const juce::Colour surface  { 0xfffffdff };    // plugin card
    const juce::Colour panel    { 0xffffffff };    // panel cards
    const juce::Colour line     { 0xffefeaf3 };    // borders
    const juce::Colour line2    { 0xffe7e0ee };    // stronger borders / knob track
    const juce::Colour chip     { 0xfff7f3f9 };    // readout / chip fills
    const juce::Colour iconBg   { 0xfff6f1f8 };    // section-header icon square
}

//--- shared card geometry ---
inline constexpr float kPanelRadius = 18.0f;

//--- soft diffuse drop shadow (dashboard look) — a blurred lilac-anthracite halo
//    behind a rounded card.  Replaces the old flat offset second-colour block. ---
inline void drawSoftShadow(juce::Graphics& g, juce::Rectangle<float> r,
                           float radius = kPanelRadius)
{
    juce::Path p;
    p.addRoundedRectangle(r, radius);
    //--- two stacked passes: a soft wide halo + a tighter contact shadow ---
    juce::DropShadow(juce::Colour(0x3a6a4f86), 26, { 0, 12 }).drawForPath(g, p);
    juce::DropShadow(juce::Colour(0x22473159),  8, { 0, 3  }).drawForPath(g, p);
}

//--- white rounded card sitting on a soft drop shadow ---
inline void drawSoftPanel(juce::Graphics& g, juce::Rectangle<float> r,
                          float radius = kPanelRadius)
{
    drawSoftShadow(g, r, radius);
    g.setColour(col::panel);
    g.fillRoundedRectangle(r, radius);
    g.setColour(col::line);
    g.drawRoundedRectangle(r, radius, 1.0f);
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
