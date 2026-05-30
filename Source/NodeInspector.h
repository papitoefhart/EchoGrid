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
    juce::Slider     gainSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider     panSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextButton reverseBtn { "REV" };
    juce::TextButton activeBtn  { "ON" };
    juce::Label      probDisplay;

    bool syncing = false;

    //--- multi-select delta drag tracking ---
    bool  gainDragging = false, panDragging = false;
    float gainRefAtStart = 0.5f, panRefAtStart = 0.0f;
    std::vector<float> gainAtDragStart, panAtDragStart;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeInspector)
};
