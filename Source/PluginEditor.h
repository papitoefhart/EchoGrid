#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "NodeTimeline.h"
#include "NodeInspector.h"

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

    //--- subdivision buttons: 4 8 16 32 steps ---
    juce::TextButton subdivBtns[4];
    static constexpr int subdivOptions[4] = { 4, 8, 16, 32 };

    juce::TextButton  panModeBtn { "PAN" };
    UndoArrowButton   undoBtn    { false };  // ↺ undo
    UndoArrowButton   redoBtn    { true  };  // ↻ redo

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EchoGridEditor)
};
