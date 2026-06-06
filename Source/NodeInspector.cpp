#include "NodeInspector.h"
#include "NodeTimeline.h"

//==============================================================================
// Helpers
//==============================================================================
static void styleKnob(juce::Slider& s)
{
    s.setColour(juce::Slider::rotarySliderFillColourId,    juce::Colour(0xff334488));
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff1a1a30));
    s.setColour(juce::Slider::thumbColourId,               juce::Colour(0xff8899ff));
}

static void styleButton(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff1a1a2e));
    b.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xff444488));
    b.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff5566aa));
    b.setColour(juce::TextButton::textColourOnId,  juce::Colour(0xffe0e0ff));
}

//==============================================================================
NodeInspector::NodeInspector(EchoGridProcessor& p, NodeTimeline& t)
    : processor(p), timeline(t)
{
    //--- node label ---
    nodeLabel.setFont(juce::Font(10.0f));
    nodeLabel.setColour(juce::Label::textColourId, juce::Colour(0xff5566aa));
    nodeLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(nodeLabel);

    //--- gain knob ---
    gainSlider.setRange(0.0, 1.0, 0.01);
    gainSlider.setDoubleClickReturnValue(true, 0.5);
    styleKnob(gainSlider);
    gainSlider.onDragStart = [this]
    {
        gainDragging    = true;
        timeline.captureSnapshot();
        gainRefAtStart  = (float)gainSlider.getValue();
        gainAtDragStart.clear();
        for (int i : timeline.getMultiSelection())
            if (i >= 0 && i < (int)processor.nodes.size())
                gainAtDragStart.push_back(processor.nodes[i].gain);
    };
    gainSlider.onDragEnd = [this] { gainDragging = false; timeline.pushUndoIfChanged(); };
    gainSlider.onValueChange = [this]
    {
        if (syncing) return;
        float val = (float)gainSlider.getValue();
        int   idx = timeline.getSelectedIndex();
        const auto& sel = timeline.getMultiSelection();
        const juce::ScopedLock sl(processor.getCallbackLock());
        if (idx == -2)
        {
            processor.dry.gain = val;
        }
        else if (sel.size() <= 1)
        {
            if (idx >= 0 && idx < (int)processor.nodes.size())
                processor.nodes[idx].gain = val;
        }
        else
        {
            //--- delta: each node shifts by the same amount from its snapshot ---
            float delta = val - gainRefAtStart;
            for (int k = 0; k < (int)std::min(sel.size(), gainAtDragStart.size()); ++k)
                if (sel[k] >= 0 && sel[k] < (int)processor.nodes.size())
                    processor.nodes[sel[k]].gain = juce::jlimit(0.0f, 1.0f, gainAtDragStart[k] + delta);
        }
        timeline.repaint();
    };
    addAndMakeVisible(gainSlider);

    //--- pan knob ---
    panSlider.setRange(-1.0, 1.0, 0.01);
    panSlider.setDoubleClickReturnValue(true, 0.0);
    styleKnob(panSlider);
    panSlider.onDragStart = [this]
    {
        panDragging    = true;
        timeline.captureSnapshot();
        panRefAtStart  = (float)panSlider.getValue();
        panAtDragStart.clear();
        for (int i : timeline.getMultiSelection())
            if (i >= 0 && i < (int)processor.nodes.size())
                panAtDragStart.push_back(processor.nodes[i].pan);
    };
    panSlider.onDragEnd = [this] { panDragging = false; timeline.pushUndoIfChanged(); };
    panSlider.onValueChange = [this]
    {
        if (syncing) return;
        float val = (float)panSlider.getValue();
        int   idx = timeline.getSelectedIndex();
        const auto& sel = timeline.getMultiSelection();
        const juce::ScopedLock sl(processor.getCallbackLock());
        if (idx == -2)
        {
            processor.dry.pan = val;
        }
        else if (sel.size() <= 1)
        {
            if (idx >= 0 && idx < (int)processor.nodes.size())
                processor.nodes[idx].pan = val;
        }
        else
        {
            float delta = val - panRefAtStart;
            for (int k = 0; k < (int)std::min(sel.size(), panAtDragStart.size()); ++k)
                if (sel[k] >= 0 && sel[k] < (int)processor.nodes.size())
                    processor.nodes[sel[k]].pan = juce::jlimit(-1.0f, 1.0f, panAtDragStart[k] + delta);
        }
        timeline.repaint();
    };
    addAndMakeVisible(panSlider);

    //--- reverse button ---
    styleButton(reverseBtn);
    reverseBtn.setClickingTogglesState(true);
    reverseBtn.onClick = [this]
    {
        if (syncing) return;
        const auto& sel = timeline.getMultiSelection();
        if (sel.empty()) return;
        timeline.captureSnapshot();
        bool newState = reverseBtn.getToggleState();
        {
            const juce::ScopedLock sl(processor.getCallbackLock());
            for (int i : sel)
                if (i >= 0 && i < (int)processor.nodes.size())
                    processor.nodes[i].reverse = newState;
        }
        timeline.pushUndoIfChanged();
    };
    addAndMakeVisible(reverseBtn);

    //--- active button: applies to all selected nodes, supports undo ---
    styleButton(activeBtn);
    activeBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff1a3322));
    activeBtn.setColour(juce::TextButton::textColourOnId,   juce::Colour(0xff88ee88));
    activeBtn.setClickingTogglesState(true);
    activeBtn.onClick = [this]
    {
        if (syncing) return;
        const auto& sel = timeline.getMultiSelection();
        if (sel.empty()) return;

        timeline.captureSnapshot();
        bool newState = activeBtn.getToggleState();
        {
            const juce::ScopedLock sl(processor.getCallbackLock());
            for (int i : sel)
                if (i >= 0 && i < (int)processor.nodes.size())
                    processor.nodes[i].active = newState;
        }
        timeline.pushUndoIfChanged();
    };
    addAndMakeVisible(activeBtn);

    //--- probability display (read-only; value set by scroll wheel on timeline) ---
    probDisplay.setJustificationType(juce::Justification::centred);
    probDisplay.setColour(juce::Label::backgroundColourId, juce::Colour(0xff1e1e34));
    probDisplay.setColour(juce::Label::textColourId,       juce::Colour(0xff8899cc));
    addAndMakeVisible(probDisplay);

    startTimerHz(30);
}

NodeInspector::~NodeInspector() { stopTimer(); }

void NodeInspector::mouseDown(const juce::MouseEvent&)
{
    // Background click (not on a child control): clear timeline selection
    timeline.clearSelection();
    repaint();
}

//==============================================================================
// Layout
//==============================================================================
void NodeInspector::resized()
{
    const int cy = getHeight() / 2;
    const int kw = 56;   // knob size (square)
    const int bh = 26;
    const int bw = 52;

    nodeLabel.setBounds(10,  cy - 18, 82, 36);
    gainSlider.setBounds(104, cy - kw / 2, kw, kw);
    panSlider.setBounds(172,  cy - kw / 2, kw, kw);
    reverseBtn.setBounds(450, cy - bh / 2, bw, bh);
    activeBtn.setBounds(512,  cy - bh / 2, bw, bh);
    probDisplay.setBounds(574, cy - bh / 2, 78, bh);
}

//==============================================================================
// Paint
//==============================================================================
void NodeInspector::paint(juce::Graphics& g)
{
    //--- panel background (slightly lighter than editor to show as a distinct area) ---
    g.fillAll(juce::Colour(0xff12121f));

    //--- top divider line ---
    g.setColour(juce::Colour(0xff4a4a7a));
    g.fillRect(0, 0, getWidth(), 1);

    int idx = timeline.getSelectedIndex();

    if (idx == -1)
    {
        g.setColour(juce::Colour(0xff3a3a5a));
        g.setFont(11.0f);
        g.drawText("— click a node to inspect —",
                   getLocalBounds(), juce::Justification::centred);
        return;
    }

    //--- section labels above each control ---
    g.setColour(juce::Colour(0xff5566aa));
    g.setFont(10.0f);

    auto labelAbove = [&](juce::Rectangle<int> r, const juce::String& text)
    {
        g.drawText(text, r.withY(r.getY() - 18).withHeight(14),
                   juce::Justification::centredLeft);
    };

    labelAbove(gainSlider.getBounds(), "GAIN");
    labelAbove(panSlider.getBounds(),  "PAN");
    if (idx >= 0) labelAbove(reverseBtn.getBounds(), "MODE");
    if (idx >= 0) labelAbove(probDisplay.getBounds(), "PROB");

    //--- value readouts below each knob ---
    g.setColour(juce::Colour(0xff8899cc));
    g.setFont(9.0f);

    auto valueBelow = [&](juce::Rectangle<int> r, const juce::String& text)
    {
        g.drawText(text, r.getX() - 6, r.getBottom() + 2, r.getWidth() + 12, 12,
                   juce::Justification::centred);
    };

    valueBelow(gainSlider.getBounds(),
               juce::String((float)gainSlider.getValue(), 2));

    float pan = (float)panSlider.getValue();
    juce::String panStr = (std::abs(pan) < 0.01f) ? "C"
                        : (pan < 0.0f) ? "L " + juce::String(-pan, 2)
                                       : "R " + juce::String( pan, 2);
    valueBelow(panSlider.getBounds(), panStr);

}

//==============================================================================
// Timer: sync UI → processor values
//==============================================================================
void NodeInspector::timerCallback()
{
    syncFromProcessor(timeline.getSelectedIndex());
}

void NodeInspector::syncFromProcessor(int idx)
{
    bool isDry  = (idx == -2);
    bool isEcho = (idx >= 0 && idx < (int)processor.nodes.size());

    //--- show/hide ---
    gainSlider.setVisible(isDry || isEcho);
    panSlider.setVisible(isDry || isEcho);
    reverseBtn.setVisible(isEcho);
    activeBtn.setVisible(isEcho);
    probDisplay.setVisible(isEcho);
    nodeLabel.setVisible(isDry || isEcho);

    syncing = true;

    if (isDry)
    {
        nodeLabel.setText("DRY", juce::dontSendNotification);
        gainSlider.setValue(processor.dry.gain, juce::dontSendNotification);
        panSlider.setValue(processor.dry.pan,   juce::dontSendNotification);
    }
    else if (isEcho)
    {
        const auto& n = processor.nodes[idx];
        nodeLabel.setText("@ " + juce::String(n.positionBeats + 1.0f, 2) + "b",
                          juce::dontSendNotification);
        if (!gainDragging) gainSlider.setValue(n.gain, juce::dontSendNotification);
        if (!panDragging)  panSlider.setValue(n.pan,  juce::dontSendNotification);
        reverseBtn.setToggleState(n.reverse, juce::dontSendNotification);
        activeBtn.setToggleState(n.active,   juce::dontSendNotification);
        probDisplay.setText(juce::String((int)std::round(n.probability * 100)) + "%",
                            juce::dontSendNotification);
    }

    syncing = false;
    repaint();
    timeline.repaint();   // keep timeline in sync with any state loaded by the host
}
