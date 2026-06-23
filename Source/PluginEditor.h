#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"
#include "NodeTimeline.h"
#include "NodeInspector.h"

//==============================================================================
// EchoGridLAF — the light "modern/calm" look (v0.25): soft knobs, rounded
// pill buttons, light combo boxes.  Set once on the editor; all child
// sliders/buttons/combos (incl. the inspector's) inherit it.
//==============================================================================
class EchoGridLAF : public juce::LookAndFeel_V4
{
public:
    EchoGridLAF();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider&) override;

    void drawComboBox(juce::Graphics&, int w, int h, bool isDown,
                      int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
};

//==============================================================================
// PillSwitch — iOS-style sliding pastel toggle.  Drop-in juce::Button so the
// existing toggle wiring (onClick / getToggleState / setClickingTogglesState)
// is unchanged; the text label is drawn by the panel, not the switch.
//==============================================================================
class PillSwitch : public juce::Button
{
public:
    PillSwitch() : juce::Button("") { setClickingTogglesState(true); }
    void setOnColour(juce::Colour c) { onColour = c; }
    void paintButton(juce::Graphics&, bool mouseOver, bool isDown) override;
private:
    juce::Colour onColour { eg::col::lilac };
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
    EchoGridLAF        egLAF;                 // declared first so children outlive it last
    juce::UndoManager  undoManager { 50 };    // max 50 undo steps; declared before timeline
    NodeTimeline       timeline;
    NodeInspector      inspector;
    juce::Slider       grainSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };

    //--- panel rectangles computed in resized(), painted in paint() ---
    juce::Rectangle<int> timelineArea, globalArea, inspectorArea;

    //--- grid length buttons: 1 2 4 8 beats ---
    juce::TextButton beatBtns[4];
    static constexpr int beatOptions[4] = { 1, 2, 4, 8 };

    //--- subdivision ComboBox: musical grid values including triplets ---
    juce::ComboBox   subdivBox;

    juce::ComboBox    layerBox;   // selects automation overlay: gain / pan / sat
    UndoArrowButton   undoBtn    { false };  // ↺ undo
    UndoArrowButton   redoBtn    { true  };  // ↻ redo

    //--- filter knobs ---
    juce::Slider hpSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider lpSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    PillSwitch   filterDryBtn;

    //--- global tape drive ---
    juce::Slider driveSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    PillSwitch   satGlobalBtn;

    //--- global input / output trim (dB on the knob, linear gain in the processor) ---
    juce::Slider inputSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider outputSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EchoGridEditor)
};
