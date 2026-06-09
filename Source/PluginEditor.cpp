#include "PluginEditor.h"

static constexpr int kBuildVersion = 22;

//==============================================================================
// Musical subdivision options — label shown in ComboBox, beats = snap step in beats
//==============================================================================
struct SubdivOption { const char* label; float beats; };
static const SubdivOption kSubdivs[] = {
    { "1/4",    1.0f        },
    { "1/4T",   2.0f/3.0f  },  // quarter triplet
    { "1/8",    0.5f        },
    { "1/8T",   1.0f/3.0f  },  // eighth triplet
    { "1/16",   0.25f       },
    { "1/16T",  1.0f/6.0f  },  // sixteenth triplet
    { "1/32",   0.125f      },
    { "1/32T",  1.0f/12.0f },
    { "1/64",   0.0625f     },
    { "1/64T",  1.0f/24.0f },
    { "1/128",  0.03125f    },
    { "1/128T", 1.0f/48.0f },
};
static constexpr int kNumSubdivs = 12;

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

    //--- subdivision ComboBox: each entry is a musical subdivision value.
    //    The beat value is the snap step in beats (e.g. 0.25 = 1/16 note).
    //    T-suffix entries are triplets. ---
    subdivBox.setLookAndFeel(&darkComboLAF);
    for (int i = 0; i < kNumSubdivs; ++i)
        subdivBox.addItem(kSubdivs[i].label, i + 1);
    subdivBox.setSelectedId(5, juce::dontSendNotification);  // 1/16 default
    subdivBox.onChange = [this]
    {
        int id = subdivBox.getSelectedId();
        if (id >= 1 && id <= kNumSubdivs)
        {
            timeline.captureSnapshot();
            processorRef.snapStepBeats = kSubdivs[id - 1].beats;
            timeline.pushUndoIfChanged();
            updateGridButtons();
        }
    };
    addAndMakeVisible(subdivBox);

    //--- analog timing jitter knob (disabled) ---
    //    this control is preserved in the UI but disabled while reverse
    //    latency behavior is being stabilized.
    analogSlider.setRange(0.0, 1.0, 0.01);
    analogSlider.setDoubleClickReturnValue(true, 0.0);
    analogSlider.setValue(p.analogAmount, juce::dontSendNotification);
    analogSlider.setColour(juce::Slider::rotarySliderFillColourId,    juce::Colour(0xff6633aa));
    analogSlider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff222238));
    analogSlider.setColour(juce::Slider::thumbColourId,               juce::Colour(0xffbb88ff));
    analogSlider.setEnabled(false);
    analogSlider.setTooltip("Disabled until reverse timing is stabilized");
    analogSlider.onValueChange = [this]
    {
        processorRef.analogAmount = (float)analogSlider.getValue();
    };
    addAndMakeVisible(analogSlider);

    //--- layer selector: picks which automation overlay is shown ---
    layerBox.setLookAndFeel(&darkComboLAF);
    layerBox.addItem("GAIN", 1);
    layerBox.addItem("PAN", 2);
    layerBox.addItem("SAT", 3);
    layerBox.setSelectedId(1, juce::dontSendNotification);
    layerBox.onChange = [this] {
        int id = layerBox.getSelectedId();
        if      (id == 2) timeline.setEditMode(NodeTimeline::EditMode::Pan);
        else if (id == 3) timeline.setEditMode(NodeTimeline::EditMode::Sat);
        else              timeline.setEditMode(NodeTimeline::EditMode::None);
    };
    addAndMakeVisible(layerBox);

    //--- undo / redo buttons (paint themselves as vector arrows) ---
    undoBtn.onClick = [this] { undoManager.undo(); timeline.repaint(); };
    addAndMakeVisible(undoBtn);

    redoBtn.onClick = [this] { undoManager.redo(); timeline.repaint(); };
    addAndMakeVisible(redoBtn);

    //--- HP filter knob ---
    hpSlider.setNormalisableRange(juce::NormalisableRange<double>(20.0, 8000.0, 1.0, 0.35));
    hpSlider.setValue(p.hpCutoffHz, juce::dontSendNotification);
    hpSlider.setDoubleClickReturnValue(true, 20.0);
    hpSlider.setColour(juce::Slider::rotarySliderFillColourId,    juce::Colour(0xff334488));
    hpSlider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff222238));
    hpSlider.setColour(juce::Slider::thumbColourId,               juce::Colour(0xff8899ff));
    hpSlider.onValueChange = [this] { processorRef.hpCutoffHz = (float)hpSlider.getValue(); };
    addAndMakeVisible(hpSlider);

    //--- LP filter knob ---
    lpSlider.setNormalisableRange(juce::NormalisableRange<double>(200.0, 20000.0, 1.0, 0.35));
    lpSlider.setValue(p.lpCutoffHz, juce::dontSendNotification);
    lpSlider.setDoubleClickReturnValue(true, 20000.0);
    lpSlider.setColour(juce::Slider::rotarySliderFillColourId,    juce::Colour(0xff334488));
    lpSlider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff222238));
    lpSlider.setColour(juce::Slider::thumbColourId,               juce::Colour(0xff8899ff));
    lpSlider.onValueChange = [this] { processorRef.lpCutoffHz = (float)lpSlider.getValue(); };
    addAndMakeVisible(lpSlider);

    //--- filter-dry toggle ---
    styleGridBtn(filterDryBtn);
    filterDryBtn.setClickingTogglesState(true);
    filterDryBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff223366));
    filterDryBtn.setColour(juce::TextButton::textColourOnId,   juce::Colour(0xffaabbff));
    filterDryBtn.onClick = [this] { processorRef.filterDry = filterDryBtn.getToggleState(); };
    addAndMakeVisible(filterDryBtn);


    addAndMakeVisible(timeline);
    addAndMakeVisible(inspector);
    setResizable(true, true);
    setResizeLimits(760, 340, 1600, 900);
    setSize(960, 420);
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
    //--- sync sliders if state was loaded externally (preset, DAW recall) ---
    auto syncf = [](juce::Slider& s, float v) {
        if (std::abs((float)s.getValue() - v) > 0.001f)
            s.setValue(v, juce::dontSendNotification);
    };
    syncf(analogSlider, processorRef.analogAmount);
    syncf(hpSlider,     processorRef.hpCutoffHz);
    syncf(lpSlider,     processorRef.lpCutoffHz);
    filterDryBtn.setToggleState(processorRef.filterDry, juce::dontSendNotification);

    updateGridButtons();
}

void EchoGridEditor::updateGridButtons()
{
    //--- beat length buttons ---
    int curLen = (int)processorRef.gridLengthBeats;
    for (int i = 0; i < 4; ++i)
    {
        bool on = (beatOptions[i] == curLen);
        beatBtns[i].setColour(juce::TextButton::textColourOffId,
                              on ? juce::Colour(0xffe0e0ff) : juce::Colour(0xff5566aa));
        beatBtns[i].setColour(juce::TextButton::buttonColourId,
                              on ? juce::Colour(0xff2a2a50) : juce::Colour(0xff1a1a2e));
    }

    //--- subdivision ComboBox: select the entry matching the current snap step ---
    float cur = processorRef.snapStepBeats;
    for (int i = 0; i < kNumSubdivs; ++i)
    {
        if (std::abs(kSubdivs[i].beats - cur) < 0.0001f)
        {
            subdivBox.setSelectedId(i + 1, juce::dontSendNotification);
            break;
        }
    }
}

//==============================================================================
void EchoGridEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0d0d1a));

    g.setColour(juce::Colour(0xff5555aa));
    g.setFont(13.0f);
    g.drawText("ECHO GRID", 16, 0, 90, 60, juce::Justification::centredLeft, false);

    g.setColour(juce::Colours::white);
    g.setFont(8.0f);
    g.drawText("v" + juce::String(kBuildVersion), 16, 44, 40, 12,
               juce::Justification::centredLeft, false);

    //--- section labels for grid controls ---
    g.setFont(9.0f);
    g.setColour(juce::Colour(0xff3a3a6a));
    g.drawText("BEATS", beatBtns[0].getX(), 9, 100, 10,
               juce::Justification::left, false);
    g.drawText("GRID",  subdivBox.getX(), 9, 80, 10,
               juce::Justification::left, false);
    g.drawText("ANALOG",  analogSlider.getX() - 4, analogSlider.getBottom() + 2, 62, 10,
               juce::Justification::centred, false);
    g.drawText("HP",      hpSlider.getX(),          hpSlider.getBottom() + 2,  48, 10,
               juce::Justification::centred, false);
    g.drawText("LP",      lpSlider.getX(),           lpSlider.getBottom() + 2,  48, 10,
               juce::Justification::centred, false);
    g.drawText("LAYER", layerBox.getX(), 9, 50, 10, juce::Justification::left, false);
}

//==============================================================================
void EchoGridEditor::resized()
{
    auto area   = getLocalBounds();
    auto topBar = area.removeFromTop(60);
    inspector.setBounds(area.removeFromBottom(100));
    timeline.setBounds(area);

    //--- analog knob (rightmost) ---
    auto knobCol = topBar.removeFromRight(68);
    analogSlider.setBounds(knobCol.getX() + 4, 3, 54, 54);

    //--- beat length buttons ---
    int bx = 120, by = 21, bw = 26, bh = 18;
    for (int i = 0; i < 4; ++i)
        beatBtns[i].setBounds(bx + i * (bw + 2), by, bw, bh);

    //--- subdivision ComboBox ---
    int sx = bx + 4 * (bw + 2) + 14;
    subdivBox.setBounds(sx, by, 76, bh);

    //--- layer selector ---
    layerBox.setBounds(sx + 78, by, 60, bh);

    //--- undo / redo buttons ---
    int ux = layerBox.getRight() + 14;
    undoBtn.setBounds(ux,      by, 26, bh);
    redoBtn.setBounds(ux + 28, by, 26, bh);

    //--- filter knobs + filt-dry button ---
    int fx = redoBtn.getRight() + 20;
    hpSlider.setBounds(fx,      3, 48, 48);
    lpSlider.setBounds(fx + 54, 3, 48, 48);
    filterDryBtn.setBounds(fx + 110, by, 52, bh);

}
