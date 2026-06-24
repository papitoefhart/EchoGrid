#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class NodeTimeline;

class NodeInspector : public juce::Component, private juce::Timer
{
public:
    NodeInspector(EchoGridProcessor& p, NodeTimeline& t);
    ~NodeInspector() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void syncFromProcessor(int idx);

    EchoGridProcessor& processor;
    NodeTimeline&      timeline;

    //--- controls ---
    juce::Label      nodeLabel;
    juce::Slider     gainSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider     panSlider   { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider     satSlider   { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider     pitchSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider     revLenSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextButton revLockBtn { "IN TIME" };   // toggles IN TIME / FREE
    juce::TextButton reverseBtn { "REV" };
    juce::TextButton activeBtn  { "ON" };

    bool syncing = false;

    //--- repaint only when what we display actually changed (the 30 Hz timer used to
    //    repaint the inspector AND the whole timeline unconditionally) ---
    juce::uint64 lastInspectorSig = 0, lastModelSig = 0;
    juce::uint64 inspectorSig(int idx) const;
    juce::uint64 modelSig() const;

    //--- multi-select delta drag tracking ---
    bool  gainDragging = false, panDragging = false, revLenDragging = false;
    bool  satDragging = false, pitchDragging = false;
    float gainRefAtStart = 0.5f, panRefAtStart = 0.0f, revLenRefAtStart = 1.0f;
    float satRefAtStart = 0.0f, pitchRefAtStart = 0.0f;
    std::vector<float> gainAtDragStart, panAtDragStart, revLenAtDragStart;
    std::vector<float> satAtDragStart, pitchAtDragStart;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeInspector)
};
