#include "PluginEditor.h"

static const juce::String kVersionLabel = "0.38";

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
// EchoGridLAF — light "modern/calm" look
//==============================================================================
EchoGridLAF::EchoGridLAF()
{
    setColour(juce::PopupMenu::backgroundColourId,            eg::col::panel);
    setColour(juce::PopupMenu::textColourId,                 eg::col::ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, eg::col::lilacSoft);
    setColour(juce::PopupMenu::highlightedTextColourId,      eg::col::ink);
}

void EchoGridLAF::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                   float sliderPos, float startAngle, float endAngle,
                                   juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(3.0f);
    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();
    const float radius  = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float toAngle = startAngle + sliderPos * (endAngle - startAngle);

    juce::Colour fill = slider.isEnabled()
                      ? slider.findColour(juce::Slider::rotarySliderFillColourId)
                      : eg::col::line2;

    const float lineW   = juce::jmin(6.0f, radius * 0.30f);
    const float arcR    = radius - lineW * 0.5f;

    //--- track arc ---
    juce::Path track;
    track.addCentredArc(cx, cy, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour(eg::col::line2);
    g.strokePath(track, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    //--- value arc ---
    if (sliderPos > 0.001f)
    {
        juce::Path val;
        val.addCentredArc(cx, cy, arcR, arcR, 0.0f, startAngle, toAngle, true);
        g.setColour(fill);
        g.strokePath(val, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    //--- soft cap ---
    const float capR = arcR - lineW - 2.0f;
    juce::Rectangle<float> cap(cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white, cx, cy - capR,
                                           juce::Colour(0xfff3eef6), cx, cy + capR, false));
    g.fillEllipse(cap);
    g.setColour(eg::col::line2);
    g.drawEllipse(cap, 1.0f);

    //--- pointer ---
    juce::Path ptr;
    const float ptrLen = capR * 0.82f;
    ptr.addRoundedRectangle(-1.4f, -ptrLen, 2.8f, ptrLen * 0.55f, 1.4f);
    g.setColour(slider.isEnabled() ? fill : eg::col::ink3);
    g.fillPath(ptr, juce::AffineTransform::rotation(toAngle).translated(cx, cy));
}

void EchoGridLAF::drawComboBox(juce::Graphics& g, int w, int h, bool /*isDown*/,
                               int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float>(0.0f, 0.0f, (float)w, (float)h).reduced(0.5f);
    g.setColour(eg::col::panel);
    g.fillRoundedRectangle(r, 9.0f);
    g.setColour(box.hasKeyboardFocus(true) ? eg::col::lilac : eg::col::line2);
    g.drawRoundedRectangle(r, 9.0f, 1.0f);

    //--- chevron ---
    const float chX = w - 15.0f, chY = h * 0.5f;
    juce::Path chev;
    chev.startNewSubPath(chX - 3.5f, chY - 2.0f);
    chev.lineTo(chX,        chY + 2.5f);
    chev.lineTo(chX + 3.5f, chY - 2.0f);
    g.setColour(eg::col::ink3);
    g.strokePath(chev, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

juce::Font EchoGridLAF::getComboBoxFont(juce::ComboBox&) { return juce::Font(12.5f); }

void EchoGridLAF::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(11, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setColour(juce::Label::textColourId, eg::col::ink);
    label.setJustificationType(juce::Justification::centredLeft);
}

void EchoGridLAF::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    g.fillAll(eg::col::panel);
    g.setColour(eg::col::line2);
    g.drawRect(0, 0, w, h, 1);
}

void EchoGridLAF::drawButtonBackground(juce::Graphics& g, juce::Button& b,
                                       const juce::Colour&, bool over, bool /*down*/)
{
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const float radius = juce::jmin(9.0f, r.getHeight() * 0.5f);

    if (b.getToggleState())
    {
        g.setColour(b.findColour(juce::TextButton::buttonOnColourId));
        g.fillRoundedRectangle(r, radius);
    }
    else
    {
        g.setColour(over ? eg::col::lilacSoft.withAlpha(0.35f) : eg::col::panel);
        g.fillRoundedRectangle(r, radius);
        g.setColour(eg::col::line2);
        g.drawRoundedRectangle(r, radius, 1.0f);
    }
}

void EchoGridLAF::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour(b.getToggleState() ? b.findColour(juce::TextButton::textColourOnId)
                                   : b.findColour(juce::TextButton::textColourOffId));
    g.setFont(juce::Font(12.0f));
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

//==============================================================================
// PillSwitch — sliding pastel toggle
//==============================================================================
void PillSwitch::paintButton(juce::Graphics& g, bool /*over*/, bool /*down*/)
{
    auto r = getLocalBounds().toFloat();
    const float h  = r.getHeight();
    const bool  on = getToggleState();

    g.setColour(on ? onColour : eg::col::line2);
    g.fillRoundedRectangle(r, h * 0.5f);

    const float kr = h - 5.0f;
    const float kx = on ? r.getRight() - kr - 2.5f : r.getX() + 2.5f;
    g.setColour(juce::Colours::white);
    g.fillEllipse(kx, r.getY() + 2.5f, kr, kr);
    g.setColour(juce::Colour(0x22000000));
    g.drawEllipse(kx, r.getY() + 2.5f, kr, kr, 0.8f);
}

//==============================================================================
// UndoArrowButton — vector-drawn curved arrow, light style
//==============================================================================
void UndoArrowButton::paintButton(juce::Graphics& g, bool mouseOver, bool isDown)
{
    const auto b = getLocalBounds().toFloat().reduced(0.5f);

    g.setColour(isDown    ? eg::col::lilacSoft :
                mouseOver ? eg::col::line :
                            eg::col::panel);
    g.fillRoundedRectangle(b, 8.0f);
    g.setColour(eg::col::line2);
    g.drawRoundedRectangle(b, 8.0f, 1.0f);

    g.setColour(eg::col::ink2);

    const float cx = b.getCentreX();
    const float cy = b.getCentreY() + 0.5f;
    const float rx = b.getWidth()  * 0.24f;
    const float ry = b.getHeight() * 0.22f;
    const float ah = 3.0f;

    juce::Path arc;
    if (!isRedo)
    {
        arc.startNewSubPath(cx + rx, cy + ry * 0.5f);
        arc.cubicTo(cx + rx, cy - ry * 1.1f, cx - rx, cy - ry * 1.1f, cx - rx, cy + ry * 0.5f);
        g.strokePath(arc, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        juce::Path head;
        head.addTriangle(cx - rx - ah, cy + ry * 0.5f - ah * 0.4f,
                         cx - rx + ah, cy + ry * 0.5f - ah * 0.4f,
                         cx - rx,      cy + ry * 0.5f + ah * 1.0f);
        g.fillPath(head);
    }
    else
    {
        arc.startNewSubPath(cx - rx, cy + ry * 0.5f);
        arc.cubicTo(cx - rx, cy - ry * 1.1f, cx + rx, cy - ry * 1.1f, cx + rx, cy + ry * 0.5f);
        g.strokePath(arc, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
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
static void styleBeatBtn(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonOnColourId, eg::col::lilac);
    b.setColour(juce::TextButton::textColourOnId,   eg::col::ink);
    b.setColour(juce::TextButton::textColourOffId,  eg::col::ink2);
}

//==============================================================================
EchoGridEditor::EchoGridEditor(EchoGridProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p), timeline(p, undoManager), inspector(p, timeline)
{
    setLookAndFeel(&egLAF);

    //--- beat length buttons ---
    const juce::String beatLabels[4] = { "1", "2", "4", "8" };
    for (int i = 0; i < 4; ++i)
    {
        beatBtns[i].setButtonText(beatLabels[i]);
        beatBtns[i].setClickingTogglesState(false);
        styleBeatBtn(beatBtns[i]);
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

    //--- subdivision ComboBox ---
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

    //--- layer selector ---
    layerBox.addItem("GAIN", 1);
    layerBox.addItem("PAN", 2);
    layerBox.addItem("SAT", 3);
    layerBox.addItem("PITCH", 4);
    layerBox.setSelectedId(1, juce::dontSendNotification);
    layerBox.onChange = [this] {
        int id = layerBox.getSelectedId();
        if      (id == 2) timeline.setEditMode(NodeTimeline::EditMode::Pan);
        else if (id == 3) timeline.setEditMode(NodeTimeline::EditMode::Sat);
        else if (id == 4) timeline.setEditMode(NodeTimeline::EditMode::Pitch);
        else              timeline.setEditMode(NodeTimeline::EditMode::None);
    };
    addAndMakeVisible(layerBox);

    //--- undo / redo ---
    undoBtn.onClick = [this] { undoManager.undo(); timeline.repaint(); };
    addAndMakeVisible(undoBtn);
    redoBtn.onClick = [this] { undoManager.redo(); timeline.repaint(); };
    addAndMakeVisible(redoBtn);

    //--- HP / LP filter knobs (blue) ---
    hpSlider.setNormalisableRange(juce::NormalisableRange<double>(20.0, 8000.0, 1.0, 0.35));
    hpSlider.setValue(p.hpCutoffHz, juce::dontSendNotification);
    hpSlider.setDoubleClickReturnValue(true, 20.0);
    hpSlider.setColour(juce::Slider::rotarySliderFillColourId, eg::col::blue);
    hpSlider.onValueChange = [this] { processorRef.hpCutoffHz = (float)hpSlider.getValue(); };
    addAndMakeVisible(hpSlider);

    lpSlider.setNormalisableRange(juce::NormalisableRange<double>(200.0, 20000.0, 1.0, 0.35));
    lpSlider.setValue(p.lpCutoffHz, juce::dontSendNotification);
    lpSlider.setDoubleClickReturnValue(true, 20000.0);
    lpSlider.setColour(juce::Slider::rotarySliderFillColourId, eg::col::blue);
    lpSlider.onValueChange = [this] { processorRef.lpCutoffHz = (float)lpSlider.getValue(); };
    addAndMakeVisible(lpSlider);

    //--- filter-dry toggle (blue, filter section) ---
    filterDryBtn.setOnColour(eg::col::blue);
    filterDryBtn.onClick = [this] { processorRef.filterDry = filterDryBtn.getToggleState(); };
    addAndMakeVisible(filterDryBtn);

    //--- global tape DRIVE knob (pink) ---
    driveSlider.setRange(0.0, 1.0, 0.01);
    driveSlider.setValue(p.satDrive, juce::dontSendNotification);
    driveSlider.setDoubleClickReturnValue(true, 0.0);
    driveSlider.setColour(juce::Slider::rotarySliderFillColourId, eg::col::pink);
    driveSlider.setTooltip("Tape drive on the whole output (dry + echoes). Active only when GLOBAL tape is on; otherwise use each tap's SAT slider.");
    driveSlider.onValueChange = [this] { processorRef.satDrive = (float)driveSlider.getValue(); };
    addAndMakeVisible(driveSlider);

    //--- GLOBAL tape toggle (pink) ---
    satGlobalBtn.setOnColour(eg::col::pink);
    satGlobalBtn.setTooltip("Ignore each tap's own drive; drive everything with the DRIVE knob");
    satGlobalBtn.onClick = [this] { processorRef.satGlobalOverride = satGlobalBtn.getToggleState(); };
    addAndMakeVisible(satGlobalBtn);

    //--- global INPUT / OUTPUT trim knobs (lilac = level).  Knob is in dB (±24,
    //    0 = unity); the processor stores the linear gain. ---
    //--- IN/OUT are ±24 dB trims, but the very bottom of the travel is a true MUTE
    //    (0 gain), not −24 dB — so "turned all the way down" is completely silent.
    //    Using −24 as the minus-infinity point makes decibelsToGain(−24,−24)==0 and
    //    gainToDecibels(0,−24)==−24 round-trip cleanly. ---
    auto setupGainKnob = [this](juce::Slider& s, float linearGain,
                                std::function<void(float)> setter, const juce::String& tip)
    {
        s.setNormalisableRange(juce::NormalisableRange<double>(-24.0, 24.0, 0.1));
        s.setValue(juce::Decibels::gainToDecibels(linearGain, -24.0f), juce::dontSendNotification);
        s.setDoubleClickReturnValue(true, 0.0);
        s.setColour(juce::Slider::rotarySliderFillColourId, eg::col::lilac);
        s.setTextValueSuffix(" dB");
        s.setTooltip(tip);
        s.onValueChange = [&s, setter]
            { setter(juce::Decibels::decibelsToGain((float)s.getValue(), -24.0f)); };
        addAndMakeVisible(s);
    };
    setupGainKnob(inputSlider,  p.inputGain,
                  [this](float g){ processorRef.inputGain  = g; },
                  "Input gain into the whole effect (also sets how hard saturation is driven)");
    setupGainKnob(outputSlider, p.outputGain,
                  [this](float g){ processorRef.outputGain = g; },
                  "Output gain - final level after the whole chain");

    //--- pitch-shift GRAIN length knob (ms) — temporary control for ear-tuning the
    //    pitch quality; live value drawn under the knob so it can be read back ---
    grainSlider.setRange(15.0, 100.0, 1.0);
    grainSlider.setValue(p.pitchGrainMs, juce::dontSendNotification);
    grainSlider.setDoubleClickReturnValue(true, 30.0);
    grainSlider.setColour(juce::Slider::rotarySliderFillColourId, eg::col::lilacDeep);
    grainSlider.setTooltip("Pitch-shift grain length (ms). Shorter = tighter but more warble; longer = smoother but more smear. Tune by ear and tell me the value.");
    grainSlider.onValueChange = [this] { processorRef.pitchGrainMs = (float)grainSlider.getValue(); };
    addAndMakeVisible(grainSlider);

    addAndMakeVisible(timeline);
    addAndMakeVisible(inspector);
    setResizable(true, true);
    setResizeLimits(860, 560, 1600, 900);   // min height fits the IN/OUT row in the global panel
    setSize(1040, 600);
    startTimerHz(10);
    updateGridButtons();
}

EchoGridEditor::~EchoGridEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

//==============================================================================
void EchoGridEditor::timerCallback()
{
    auto syncf = [](juce::Slider& s, float v) {
        if (std::abs((float)s.getValue() - v) > 0.001f)
            s.setValue(v, juce::dontSendNotification);
    };
    syncf(grainSlider,  processorRef.pitchGrainMs);
    syncf(hpSlider,     processorRef.hpCutoffHz);
    syncf(lpSlider,     processorRef.lpCutoffHz);
    syncf(driveSlider,  processorRef.satDrive);
    //--- gain knobs are in dB but the processor holds linear gain (−24 dB = mute) ---
    auto syncGain = [](juce::Slider& s, float linear) {
        float dB = juce::Decibels::gainToDecibels(linear, -24.0f);
        if (std::abs((float)s.getValue() - dB) > 0.01f)
            s.setValue(dB, juce::dontSendNotification);
    };
    syncGain(inputSlider,  processorRef.inputGain);
    syncGain(outputSlider, processorRef.outputGain);
    filterDryBtn.setToggleState(processorRef.filterDry,          juce::dontSendNotification);
    satGlobalBtn.setToggleState(processorRef.satGlobalOverride,  juce::dontSendNotification);

    updateGridButtons();
}

void EchoGridEditor::updateGridButtons()
{
    int curLen = (int)processorRef.gridLengthBeats;
    for (int i = 0; i < 4; ++i)
        beatBtns[i].setToggleState(beatOptions[i] == curLen, juce::dontSendNotification);

    float cur = processorRef.snapStepBeats;
    for (int i = 0; i < kNumSubdivs; ++i)
        if (std::abs(kSubdivs[i].beats - cur) < 0.0001f)
        {
            subdivBox.setSelectedId(i + 1, juce::dontSendNotification);
            break;
        }
    repaint();
}

//==============================================================================
void EchoGridEditor::paint(juce::Graphics& g)
{
    g.fillAll(eg::col::windowBg);

    //--- brand ---
    juce::Font bf(20.0f, juce::Font::bold);
    g.setFont(bf);
    const float bx = 18.0f, byTop = 16.0f;
    g.setColour(eg::col::ink);
    g.drawText("echo", (int)bx, (int)byTop, 80, 24, juce::Justification::left, false);
    const float ew = bf.getStringWidthFloat("echo");
    g.setColour(eg::col::lilacDeep);
    g.drawText("grid", (int)(bx + ew), (int)byTop, 80, 24, juce::Justification::left, false);

    g.setColour(eg::col::ink3);
    g.setFont(8.5f);
    g.drawText("MULTI-TAP DELAY", (int)bx, 41, 130, 10, juce::Justification::left, false);
    g.drawText("v" + kVersionLabel, (int)bx, 2, 60, 10, juce::Justification::left, false);

    //--- top-bar section labels ---
    g.setColour(eg::col::ink3);
    g.setFont(8.5f);
    auto sectionLabel = [&](juce::Component& c, const juce::String& t) {
        g.drawText(t, c.getX(), c.getY() - 13, 80, 10, juce::Justification::left, false);
    };
    sectionLabel(beatBtns[0], "BEATS");
    sectionLabel(subdivBox,   "GRID");
    sectionLabel(layerBox,    "LAYER");

    //--- offset second-colour shadow behind the timeline & inspector child panels ---
    auto shadowFor = [&](juce::Rectangle<int> a) {
        g.setColour(eg::col::shadow);
        g.fillRoundedRectangle(a.toFloat().translated(7.0f, 8.0f), 16.0f);
    };
    shadowFor(timelineArea);
    shadowFor(inspectorArea);

    //--- right Global panel (painted here; its knobs/pills are child controls) ---
    eg::drawSoftPanel(g, globalArea.toFloat(), 16.0f);

    g.setColour(eg::col::ink3);
    g.setFont(9.0f);
    g.drawText("GLOBAL", globalArea.getX() + 16, globalArea.getY() + 12, 120, 12,
               juce::Justification::left, false);

    //--- knob labels ---
    auto knobLabel = [&](juce::Slider& s, const juce::String& t) {
        g.setColour(eg::col::ink2);
        g.setFont(10.0f);
        g.drawText(t, s.getX() - 10, s.getBottom() + 2, s.getWidth() + 20, 12,
                   juce::Justification::centred, false);
    };
    knobLabel(hpSlider,    "HP");
    knobLabel(lpSlider,    "LP");
    knobLabel(driveSlider, "DRIVE");
    knobLabel(inputSlider,  "IN");
    knobLabel(outputSlider, "OUT");
    //--- GRAIN knob: label + live ms value (so the tuned value can be read back) ---
    g.setColour(eg::col::ink2);
    g.setFont(10.0f);
    g.drawText("GRAIN", grainSlider.getX() - 10, grainSlider.getBottom() + 2,
               grainSlider.getWidth() + 20, 12, juce::Justification::centred, false);
    g.setColour(eg::col::ink3);
    g.setFont(9.0f);
    g.drawText(juce::String((int)std::round(processorRef.pitchGrainMs)) + " ms",
               grainSlider.getX() - 10, grainSlider.getBottom() + 13,
               grainSlider.getWidth() + 20, 11, juce::Justification::centred, false);

    //--- toggle labels (to the left of each pill) ---
    g.setColour(eg::col::ink);
    g.setFont(12.0f);
    auto pillLabel = [&](juce::Component& c, const juce::String& t) {
        g.drawText(t, globalArea.getX() + 16, c.getY(), c.getX() - globalArea.getX() - 20,
                   c.getHeight(), juce::Justification::centredLeft, false);
    };
    pillLabel(filterDryBtn, "Filter dry");
    pillLabel(satGlobalBtn, "Global drive");
}

//==============================================================================
void EchoGridEditor::resized()
{
    auto area = getLocalBounds();
    const int pad = 16;
    area.reduce(pad, pad);

    auto topBar = area.removeFromTop(54);
    area.removeFromTop(12);
    inspectorArea = area.removeFromBottom(92);
    area.removeFromBottom(12);

    auto body = area;
    globalArea = body.removeFromRight(212);
    body.removeFromRight(16);
    timelineArea = body;

    timeline.setBounds(timelineArea);
    inspector.setBounds(inspectorArea);

    //--- top bar ---
    const int rowY = topBar.getY() + 20;
    const int h    = 26;
    int x = topBar.getX() + 150;

    const int bw = 28;
    for (int i = 0; i < 4; ++i)
        beatBtns[i].setBounds(x + i * (bw + 3), rowY, bw, h);
    x += 4 * (bw + 3) + 18;

    subdivBox.setBounds(x, rowY, 78, h);  x += 78 + 18;
    layerBox.setBounds(x, rowY, 88, h);

    redoBtn.setBounds(topBar.getRight() - 30,       rowY, 30, h);
    undoBtn.setBounds(topBar.getRight() - 30 - 34,  rowY, 30, h);

    //--- global panel internals ---
    const int gx     = globalArea.getX();
    const int gw     = globalArea.getWidth();
    const int innerX = gx + 16;
    const int colW   = (gw - 32) / 2;

    int ky  = globalArea.getY() + 36;
    int ks  = 50;
    hpSlider.setBounds(innerX        + (colW - ks) / 2, ky, ks, ks);
    lpSlider.setBounds(innerX + colW + (colW - ks) / 2, ky, ks, ks);

    int ky2 = ky + ks + 24;
    int ks2 = 58;
    driveSlider.setBounds (innerX        + (colW - ks2) / 2, ky2, ks2, ks2);
    grainSlider.setBounds (innerX + colW + (colW - 48)  / 2, ky2 + 4, 48, 48);

    int ty = ky2 + ks2 + 30;
    const int pillW = 42, pillH = 24;
    filterDryBtn.setBounds(globalArea.getRight() - 16 - pillW, ty,      pillW, pillH);
    satGlobalBtn.setBounds(globalArea.getRight() - 16 - pillW, ty + 34, pillW, pillH);

    //--- IN / OUT trim row at the bottom of the panel ---
    int ky3 = ty + 34 + pillH + 22;
    int ks3 = 50;
    inputSlider.setBounds (innerX        + (colW - ks3) / 2, ky3, ks3, ks3);
    outputSlider.setBounds(innerX + colW + (colW - ks3) / 2, ky3, ks3, ks3);
}
