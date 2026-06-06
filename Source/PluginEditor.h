#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "NodeTimeline.h"
#include "NodeInspector.h"

//==============================================================================
// Dark LookAndFeel: applies a dark colour scheme to the ComboBox and its popup
//==============================================================================
class DarkComboLAF : public juce::LookAndFeel_V4
{
public:
    DarkComboLAF()
    {
        setColour(juce::ComboBox::backgroundColourId,      juce::Colour(0xff1a1a2e));
        setColour(juce::ComboBox::textColourId,            juce::Colour(0xff5566aa));
        setColour(juce::ComboBox::outlineColourId,         juce::Colour(0xff2a2a50));
        setColour(juce::ComboBox::arrowColourId,           juce::Colour(0xff5566aa));
        setColour(juce::ComboBox::focusedOutlineColourId,  juce::Colour(0xff4455aa));
        setColour(juce::PopupMenu::backgroundColourId,     juce::Colour(0xff141428));
        setColour(juce::PopupMenu::textColourId,           juce::Colour(0xffccccee));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff2a2a6a));
        setColour(juce::PopupMenu::highlightedTextColourId,       juce::Colour(0xffffffff));
    }
};

//==============================================================================
// Curved undo/redo arrow button — drawn as vector paths so no font glyph needed
//==============================================================================
class UndoArrowButton : public juce::Button
{
public:
    //--- isRedo = false → ↺ undo,  true → ↻ redo ---
    UndoArrowButton(bool redo) : juce::Button(""), isRedo(redo) {}
    void paintButton(juce::Graphics& g, bool mouseOver, bool isDown) override;
private:
    bool isRedo;
};

//==============================================================================
class EchoGridEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    EchoGridEditor(EchoGridProcessor&);
    ~EchoGridEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateGridButtons();

    EchoGridProcessor& processorRef;
    juce::UndoManager  undoManager { 50 };   // max 50 undo steps; declared before timeline
    NodeTimeline       timeline;
    NodeInspector      inspector;
    juce::Slider       analogSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };

    //--- grid length buttons: 1 2 4 8 beats ---
    juce::TextButton beatBtns[4];
    static constexpr int beatOptions[4] = { 1, 2, 4, 8 };

    //--- subdivision ComboBox: musical grid values including triplets ---
    DarkComboLAF     darkComboLAF;
    juce::ComboBox   subdivBox;

    juce::ComboBox    layerBox;   // selects automation overlay: none / pan / sat
    UndoArrowButton   undoBtn    { false };  // ↺ undo
    UndoArrowButton   redoBtn    { true  };  // ↻ redo

    //--- filter knobs ---
    juce::Slider hpSlider     { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider lpSlider     { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextButton filterDryBtn { "FILT DRY" };


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EchoGridEditor)
};
