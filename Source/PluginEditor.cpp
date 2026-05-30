#include "PluginEditor.h"

//==============================================================================
// UndoArrowButton — vector-drawn curved arrow, no font glyph required
//==============================================================================
void UndoArrowButton::paintButton(juce::Graphics& g, bool mouseOver, bool isDown)
{
    const auto b = getLocalBounds().toFloat().reduced(1.0f);

    //--- background matches the grid button colour scheme ---
    g.setColour(isDown    ? juce::Colour(0xff2a2a50) :
                mouseOver ? juce::Colour(0xff1e1e3a) :
                            juce::Colour(0xff1a1a2e));
    g.fillRoundedRectangle(b, 4.0f);

    g.setColour(juce::Colour(0xff5566aa));

    const float cx = b.getCentreX();
    const float cy = b.getCentreY() + 0.5f;
    const float rx = b.getWidth()  * 0.28f;   // arc half-width
    const float ry = b.getHeight() * 0.26f;   // arc half-height
    const float ah = 3.0f;                     // arrowhead size

    //--- curved arc: Bezier approximation of an open semicircle ---
    juce::Path arc;
    if (!isRedo)
    {
        //--- undo ↺: start bottom-right, sweep up and over to bottom-left ---
        arc.startNewSubPath(cx + rx, cy + ry * 0.5f);
        arc.cubicTo(cx + rx, cy - ry * 1.1f,
                    cx - rx, cy - ry * 1.1f,
                    cx - rx, cy + ry * 0.5f);
        g.strokePath(arc, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        //--- arrowhead at the arc's end (bottom-left), pointing downward ---
        juce::Path head;
        head.addTriangle(cx - rx - ah, cy + ry * 0.5f - ah * 0.4f,
                         cx - rx + ah, cy + ry * 0.5f - ah * 0.4f,
                         cx - rx,      cy + ry * 0.5f + ah * 1.0f);
        g.fillPath(head);
    }
    else
    {
        //--- redo ↻: mirror — start bottom-left, sweep up and over to bottom-right ---
        arc.startNewSubPath(cx - rx, cy + ry * 0.5f);
        arc.cubicTo(cx - rx, cy - ry * 1.1f,
                    cx + rx, cy - ry * 1.1f,
                    cx + rx, cy + ry * 0.5f);
        g.strokePath(arc, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        //--- arrowhead at the arc's end (bottom-right), pointing downward ---
        juce::Path head;
        head.addTriangle(cx + rx - ah, cy + ry * 0.5f - ah * 0.4f,
                         cx + rx + ah, cy + ry * 0.5f - ah * 0.4f,
                         cx + rx,      cy + ry * 0.5f + ah * 1.0f);
        g.fillPath(head);
    }
}

//==============================================================================
// Helpers
//==============================================================================
static void styleGridBtn(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff1a1a2e));
    b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a3a6a));
    b.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff5566aa));
    b.setColour(juce::TextButton::textColourOnId,  juce::Colour(0xffe0e0ff));
}

//==============================================================================
EchoGridEditor::EchoGridEditor(EchoGridProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p), timeline(p, undoManager), inspector(p, timeline)
{
    //--- beat length buttons ---
    const juce::String beatLabels[4] = { "1", "2", "4", "8" };
    for (int i = 0; i < 4; ++i)
    {
        beatBtns[i].setButtonText(beatLabels[i]);
        beatBtns[i].setClickingTogglesState(false);
        styleGridBtn(beatBtns[i]);
        beatBtns[i].onClick = [this, i]
        {
            int newLen = beatOptions[i];
            timeline.captureSnapshot();
            {
                const juce::ScopedLock sl(processorRef.getCallbackLock());
                processorRef.nodes.erase(
                    std::remove_if(processorRef.nodes.begin(), processorRef.nodes.end(),
                        [newLen](const EchoNode& n){ return n.positionBeats > (float)newLen; }),
                    processorRef.nodes.end());
            }
            processorRef.gridLengthBeats = (float)newLen;
            timeline.pushUndoIfChanged();
            updateGridButtons();
        };
        addAndMakeVisible(beatBtns[i]);
    }

    //--- subdivision buttons ---
    const juce::String subdivLabels[4] = { "1/4", "1/8", "1/16", "1/32" };
    for (int i = 0; i < 4; ++i)
    {
        subdivBtns[i].setButtonText(subdivLabels[i]);
        subdivBtns[i].setClickingTogglesState(false);
        styleGridBtn(subdivBtns[i]);
        subdivBtns[i].onClick = [this, i]
        {
            processorRef.subdivisions = subdivOptions[i];
            timeline.repaint();
            updateGridButtons();
        };
        addAndMakeVisible(subdivBtns[i]);
    }

    //--- analog knob ---
    analogSlider.setRange(0.0, 1.0, 0.01);
    analogSlider.setDoubleClickReturnValue(true, 0.0);
    analogSlider.setValue(p.analogAmount, juce::dontSendNotification);
    analogSlider.setColour(juce::Slider::rotarySliderFillColourId,    juce::Colour(0xff6633aa));
    analogSlider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff222238));
    analogSlider.setColour(juce::Slider::thumbColourId,               juce::Colour(0xffbb88ff));
    analogSlider.onValueChange = [this]
    {
        processorRef.analogAmount = (float)analogSlider.getValue();
    };
    addAndMakeVisible(analogSlider);

    //--- pan mode toggle ---
    panModeBtn.setClickingTogglesState(true);
    styleGridBtn(panModeBtn);
    panModeBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff223366));
    panModeBtn.setColour(juce::TextButton::textColourOnId,   juce::Colour(0xffaabbff));
    panModeBtn.onClick = [this] { timeline.setPanEditMode(panModeBtn.getToggleState()); };
    addAndMakeVisible(panModeBtn);

    //--- undo / redo buttons (paint themselves as vector arrows) ---
    undoBtn.onClick = [this] { undoManager.undo(); timeline.repaint(); };
    addAndMakeVisible(undoBtn);

    redoBtn.onClick = [this] { undoManager.redo(); timeline.repaint(); };
    addAndMakeVisible(redoBtn);

    addAndMakeVisible(timeline);
    addAndMakeVisible(inspector);
    setResizable(true, true);
    setResizeLimits(640, 340, 1600, 900);
    setSize(800, 420);
    startTimerHz(10);
    updateGridButtons();
}

EchoGridEditor::~EchoGridEditor()
{
    stopTimer();
}

//==============================================================================
void EchoGridEditor::timerCallback()
{
    //--- sync analog slider if state was loaded externally ---
    float v = processorRef.analogAmount;
    if (std::abs((float)analogSlider.getValue() - v) > 0.001f)
        analogSlider.setValue(v, juce::dontSendNotification);

    updateGridButtons();
}

void EchoGridEditor::updateGridButtons()
{
    int   curLen    = (int)processorRef.gridLengthBeats;
    int   curSubdiv = processorRef.subdivisions;

    for (int i = 0; i < 4; ++i)
    {
        bool on = (beatOptions[i] == curLen);
        beatBtns[i].setColour(juce::TextButton::textColourOffId,
                              on ? juce::Colour(0xffe0e0ff) : juce::Colour(0xff5566aa));
        beatBtns[i].setColour(juce::TextButton::buttonColourId,
                              on ? juce::Colour(0xff2a2a50) : juce::Colour(0xff1a1a2e));
    }

    for (int i = 0; i < 4; ++i)
    {
        bool on = (subdivOptions[i] == curSubdiv);
        subdivBtns[i].setColour(juce::TextButton::textColourOffId,
                                on ? juce::Colour(0xffe0e0ff) : juce::Colour(0xff5566aa));
        subdivBtns[i].setColour(juce::TextButton::buttonColourId,
                                on ? juce::Colour(0xff2a2a50) : juce::Colour(0xff1a1a2e));
    }
}

//==============================================================================
void EchoGridEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0d0d1a));

    g.setColour(juce::Colour(0xff5555aa));
    g.setFont(13.0f);
    g.drawText("ECHO GRID", 16, 0, 90, 60, juce::Justification::centredLeft, false);

    //--- section labels for grid controls ---
    g.setFont(9.0f);
    g.setColour(juce::Colour(0xff3a3a6a));
    g.drawText("BEATS", beatBtns[0].getX(), 9, 100, 10,
               juce::Justification::left, false);
    g.drawText("GRID",  subdivBtns[0].getX(), 9, 80, 10,
               juce::Justification::left, false);
    g.drawText("ANALOG", analogSlider.getX() - 4, analogSlider.getBottom() + 2, 62, 10,
               juce::Justification::centred, false);
    g.drawText("PAN", panModeBtn.getX(), 9, 40, 10, juce::Justification::left, false);
}

//==============================================================================
void EchoGridEditor::resized()
{
    auto area   = getLocalBounds();
    auto topBar = area.removeFromTop(60);
    inspector.setBounds(area.removeFromBottom(100));
    timeline.setBounds(area);

    //--- analog knob: fits inside the 60px top bar ---
    auto knobCol = topBar.removeFromRight(62);
    analogSlider.setBounds(knobCol.getX() + 4, 3, 54, 54);

    //--- beat length buttons: centred in the 60px bar ---
    int bx = 120, by = 21, bw = 26, bh = 18;
    for (int i = 0; i < 4; ++i)
        beatBtns[i].setBounds(bx + i * (bw + 2), by, bw, bh);

    //--- subdivision buttons ---
    int sw[] = { 30, 30, 36, 36 };
    int sx = bx + 4 * (bw + 2) + 14;
    for (int i = 0; i < 4; ++i)
    {
        subdivBtns[i].setBounds(sx, by, sw[i], bh);
        sx += sw[i] + 2;
    }

    //--- pan mode button ---
    panModeBtn.setBounds(sx + 12, by, 36, bh);

    //--- undo / redo buttons ---
    int ux = panModeBtn.getRight() + 14;
    undoBtn.setBounds(ux,      by, 26, bh);
    redoBtn.setBounds(ux + 28, by, 26, bh);
}
