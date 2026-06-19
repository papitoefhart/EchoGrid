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

    //--- surfaces ---
    const juce::Colour shadow   { 0xffc2a4d6 };    // offset second-colour shadow block
    const juce::Colour windowBg { 0xfff3edf6 };    // editor background
    const juce::Colour surface  { 0xfffffdff };    // plugin card
    const juce::Colour panel    { 0xffffffff };    // panel cards
    const juce::Colour line     { 0xffefeaf3 };    // borders
    const juce::Colour line2    { 0xffe7e0ee };    // stronger borders / knob track
}

//--- flat offset second-colour shadow: lilac block behind a white rounded panel ---
inline void drawSoftPanel(juce::Graphics& g, juce::Rectangle<float> r,
                          float radius = 16.0f, float dx = 7.0f, float dy = 8.0f)
{
    g.setColour(col::shadow);
    g.fillRoundedRectangle(r.translated(dx, dy), radius);
    g.setColour(col::panel);
    g.fillRoundedRectangle(r, radius);
    g.setColour(col::line);
    g.drawRoundedRectangle(r, radius, 1.0f);
}
}
