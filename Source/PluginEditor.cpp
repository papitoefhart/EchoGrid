#include "PluginEditor.h"

static const juce::String kVersionLabel = "0.51";

//==============================================================================
// Musical subdivision options — label shown on the tab, beats = snap step in beats.
// Count must match EchoGridEditor::kNumSubdivs.
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
};

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
// GuideTab — guide-card tab merging into an edge of the timeline panel
//==============================================================================
void GuideTab::paintButton(juce::Graphics& g, bool mouseOver, bool /*isDown*/)
{
    auto r = getLocalBounds().toFloat();
    const float topR  = 8.0f;   // convex corners on the free edge
    const float footR = 5.0f;   // concave flare where the tab flows into the panel
    const bool  on    = getToggleState();

    //--- Tab outline as an OPEN path running along the three free edges only.  The
    //    panel-side edge is left open (filled by the implicit close, stroked by
    //    nothing) so there's no seam, and the base corners flare out with a concave
    //    curve so the tab melts smoothly into the panel surface. ---
    const float xL = r.getX(), xR = r.getRight();
    juce::Path p;

    if (attachTop)
    {
        const float yTop = r.getY(), yPan = r.getBottom();   // panel edge = bottom
        p.startNewSubPath(xL, yPan);
        p.quadraticTo(xL + footR, yPan, xL + footR, yPan - footR);          // concave foot
        p.lineTo(xL + footR, yTop + topR);
        p.quadraticTo(xL + footR, yTop, xL + footR + topR, yTop);          // top-left
        p.lineTo(xR - footR - topR, yTop);
        p.quadraticTo(xR - footR, yTop, xR - footR, yTop + topR);          // top-right
        p.lineTo(xR - footR, yPan - footR);
        p.quadraticTo(xR - footR, yPan, xR, yPan);                         // concave foot
    }
    else
    {
        const float yBot = r.getBottom(), yPan = r.getY();   // panel edge = top
        p.startNewSubPath(xL, yPan);
        p.quadraticTo(xL + footR, yPan, xL + footR, yPan + footR);
        p.lineTo(xL + footR, yBot - topR);
        p.quadraticTo(xL + footR, yBot, xL + footR + topR, yBot);
        p.lineTo(xR - footR - topR, yBot);
        p.quadraticTo(xR - footR, yBot, xR - footR, yBot - topR);
        p.lineTo(xR - footR, yPan + footR);
        p.quadraticTo(xR - footR, yPan, xR, yPan);
    }

    if (on)
    {
        //--- selected: lilac that fades to the panel's white near the merged edge, so
        //    the purple stops short of the panel instead of meeting it as a hard line ---
        const float fade = 8.0f;
        auto grad = attachTop
            ? juce::ColourGradient(eg::col::lilac,   r.getCentreX(), r.getBottom() - fade,
                                   eg::col::surface, r.getCentreX(), r.getBottom(), false)
            : juce::ColourGradient(eg::col::lilac,   r.getCentreX(), r.getY() + fade,
                                   eg::col::surface, r.getCentreX(), r.getY(), false);
        g.setGradientFill(grad);
        g.fillPath(p);
    }
    else
    {
        //--- unselected fill matches the timeline surface so the join is invisible ---
        g.setColour(mouseOver ? eg::col::lilacSoft.withAlpha(0.45f) : eg::col::surface);
        g.fillPath(p);   // implicitly closed straight along the panel edge
        g.setColour(eg::col::line2);
        g.strokePath(p, juce::PathStrokeType(1.0f));   // open path → no line on the panel edge
    }

    g.setColour(on ? juce::Colours::white : eg::col::ink2);
    g.setFont(juce::Font(fontSize, on ? juce::Font::bold : juce::Font::plain));
    //--- text in the body (clear of the feet and the merged edge) ---
    auto textArea = getLocalBounds().reduced((int)footR, 0);
    textArea = attachTop ? textArea.withTrimmedBottom(3) : textArea.withTrimmedTop(3);
    g.drawText(getButtonText(), textArea, juce::Justification::centred, false);
}

//==============================================================================
EchoGridEditor::EchoGridEditor(EchoGridProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p), timeline(p, undoManager), inspector(p, timeline)
{
    setLookAndFeel(&egLAF);

    //--- beat length tabs (guide cards, top-right of the timeline) ---
    const juce::String beatLabels[4] = { "1", "2", "4", "8" };
    for (int i = 0; i < 4; ++i)
    {
        beatBtns[i].setButtonText(beatLabels[i]);
        beatBtns[i].setClickingTogglesState(false);
        beatBtns[i].setAttachTop(true);
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

    //--- subdivision tabs (guide cards, bottom-left of the timeline; replaced the
    //    dropdown).  attachTop=false so they hang below the panel's bottom edge ---
    for (int i = 0; i < kNumSubdivs; ++i)
    {
        subdivTabs[i].setButtonText(kSubdivs[i].label);
        subdivTabs[i].setClickingTogglesState(false);
        subdivTabs[i].setAttachTop(false);
        subdivTabs[i].setFontSize(9.5f);
        subdivTabs[i].onClick = [this, i]
        {
            timeline.captureSnapshot();
            processorRef.snapStepBeats = kSubdivs[i].beats;
            timeline.pushUndoIfChanged();
            updateGridButtons();
        };
        addAndMakeVisible(subdivTabs[i]);
    }

    //--- layer selector: GAIN / PAN / SAT / PITCH tab cards (purple = selected,
    //    white = not), replacing the old dropdown ---
    const juce::String layerLabels[kNumLayers] = { "GAIN", "PAN", "SAT", "PITCH" };
    for (int i = 0; i < kNumLayers; ++i)
    {
        layerTabs[i].setButtonText(layerLabels[i]);
        layerTabs[i].setClickingTogglesState(false);
        layerTabs[i].onClick = [this, i] { setLayer(i); };
        addAndMakeVisible(layerTabs[i]);
    }
    setLayer(0);   // GAIN selected by default

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
    grainSlider.setColour(juce::Slider::rotarySliderFillColourId, eg::col::green);
    grainSlider.setTooltip("Pitch-shift grain length (ms). Shorter = tighter but more warble; longer = smoother but more smear. Tune by ear and tell me the value.");
    grainSlider.onValueChange = [this] { processorRef.pitchGrainMs = (float)grainSlider.getValue(); repaint(); };
    addAndMakeVisible(grainSlider);

    addAndMakeVisible(timeline);
    addAndMakeVisible(inspector);
    //--- all guide-card tabs must paint on top of the timeline panel's border so
    //    they merge into it ---
    for (auto& t : layerTabs)  t.toFront(false);
    for (auto& t : beatBtns)   t.toFront(false);
    for (auto& t : subdivTabs) t.toFront(false);

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

    //--- repaint only on real changes (e.g. host loaded a preset).  The editor's own
    //    paint draws the live GRAIN ms readout; the timeline draws the grid.  This used
    //    to repaint the WHOLE editor 10x/sec unconditionally (re-rendering ~40 drop
    //    shadows each time) — the main cause of the sluggish, laggy feel. ---
    if (std::abs(lastGrainMs - processorRef.pitchGrainMs) > 1.0e-6f)
    {
        lastGrainMs = processorRef.pitchGrainMs;
        repaint();
    }
    if (std::abs(lastGridLen  - processorRef.gridLengthBeats) > 1.0e-6f
     || std::abs(lastSnapStep - processorRef.snapStepBeats)   > 1.0e-6f)
    {
        lastGridLen  = processorRef.gridLengthBeats;
        lastSnapStep = processorRef.snapStepBeats;
        timeline.repaint();
    }
}

void EchoGridEditor::updateGridButtons()
{
    int curLen = (int)processorRef.gridLengthBeats;
    for (int i = 0; i < 4; ++i)
        beatBtns[i].setToggleState(beatOptions[i] == curLen, juce::dontSendNotification);

    float cur = processorRef.snapStepBeats;
    for (int i = 0; i < kNumSubdivs; ++i)
        subdivTabs[i].setToggleState(std::abs(kSubdivs[i].beats - cur) < 0.0001f,
                                     juce::dontSendNotification);
    //--- setToggleState already repaints any tab whose state changed; no editor-wide
    //    repaint needed here (callers that change the grid repaint the timeline). ---
}

//--- select an overlay layer (0 GAIN / 1 PAN / 2 SAT / 3 PITCH): highlight its
//    tab card and switch the timeline's edit mode ---
void EchoGridEditor::setLayer(int index)
{
    for (int i = 0; i < kNumLayers; ++i)
        layerTabs[i].setToggleState(i == index, juce::dontSendNotification);

    auto mode = index == 1 ? NodeTimeline::EditMode::Pan
              : index == 2 ? NodeTimeline::EditMode::Sat
              : index == 3 ? NodeTimeline::EditMode::Pitch
                           : NodeTimeline::EditMode::None;
    timeline.setEditMode(mode);
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

    //--- all static drop-shadows (timeline, inspector, guide-card tabs, undo/redo
    //    arrows, global panel) are pre-rendered once into shadowCache in resized() and
    //    blitted here.  Drawing them live was ~40 Gaussian blurs PER paint — and the
    //    editor used to repaint 10x/sec, which is what made clicks feel laggy. ---
    if (shadowCache.isValid())
        g.drawImageAt(shadowCache, 0, 0);

    //--- right Global panel surface (its soft shadow is baked into shadowCache above;
    //    the knobs/pills are child controls) ---
    g.setColour(eg::col::panel);
    g.fillRoundedRectangle(globalArea.toFloat(), eg::kPanelRadius);
    g.setColour(eg::col::line);
    g.drawRoundedRectangle(globalArea.toFloat(), eg::kPanelRadius, 1.0f);

    //--- GLOBAL section header: icon chip + uppercase label ---
    {
        juce::Rectangle<float> ic((float)globalArea.getX() + 16.0f,
                                  (float)globalArea.getY() + 12.0f, 22.0f, 22.0f);
        g.setColour(eg::col::iconBg);
        g.fillRoundedRectangle(ic, 7.0f);
        g.setColour(eg::col::line);
        g.drawRoundedRectangle(ic, 7.0f, 1.0f);
        eg::drawSlidersIcon(g, ic.reduced(5.5f), eg::col::lilacDeep);

        g.setColour(eg::col::ink2);
        g.setFont(juce::Font(10.5f, juce::Font::bold));
        g.drawText("GLOBAL", (int)ic.getRight() + 8, (int)ic.getY(), 120, 22,
                   juce::Justification::centredLeft, false);
    }

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
    area.removeFromBottom(36);   // gap holds the subdivision tabs hanging below the timeline

    auto body = area;
    globalArea = body.removeFromRight(212);
    body.removeFromRight(16);
    timelineArea = body;

    timeline.setBounds(timelineArea);
    inspector.setBounds(inspectorArea);

    //--- top bar: undo / redo, left-aligned with the GLOBAL panel (kept as separate
    //    floating buttons — NOT merged into the panel like the tabs) ---
    const int rowY = topBar.getY() + 20;
    const int h    = 26;
    undoBtn.setBounds(globalArea.getX(),      rowY, 30, h);
    redoBtn.setBounds(globalArea.getX() + 34, rowY, 30, h);

    //==========================================================================
    // Guide-card tabs clinging to the timeline panel edges.  Each group overlaps
    // the panel border by 2px (and is toFront'd) so it merges into the card; the
    // gaps are wide enough that the tabs' concave feet don't collide.  Every group
    // sits in empty space — never over another panel, slider or tap.
    //==========================================================================
    const int tabH = 26;

    //--- LAYER (top-left): GAIN / PAN / SAT / PITCH ---
    {
        const int w = 52, gap = 6;
        int tx = timelineArea.getX() + 22;
        int ty = timelineArea.getY() + 2 - tabH;       // hangs above, 2px into the top edge
        for (int i = 0; i < kNumLayers; ++i)
            layerTabs[i].setBounds(tx + i * (w + gap), ty, w, tabH);
    }

    //--- BEATS (top-right): 1 / 2 / 4 / 8 ---
    {
        const int w = 36, gap = 6;
        const int groupW = 4 * w + 3 * gap;
        int tx = timelineArea.getRight() - 22 - groupW;
        int ty = timelineArea.getY() + 2 - tabH;
        for (int i = 0; i < 4; ++i)
            beatBtns[i].setBounds(tx + i * (w + gap), ty, w, tabH);
    }

    //--- GRID subdivision (bottom-left): 1/4 … 1/128T, hanging below the panel ---
    {
        const int w = 44, gap = 4;
        int tx = timelineArea.getX() + 22;
        int ty = timelineArea.getBottom() - 2;         // hangs below, 2px into the bottom edge
        for (int i = 0; i < kNumSubdivs; ++i)
            subdivTabs[i].setBounds(tx + i * (w + gap), ty, w, tabH);
    }

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

    //--- positions are final → (re)bake the static drop-shadows used by paint() ---
    rebuildShadowCache();
}

//==============================================================================
// Pre-render every static drop-shadow into shadowCache once, so paint() can blit
// it instead of recomputing ~40 Gaussian blurs each frame.  Rebuilt only on resize.
//==============================================================================
void EchoGridEditor::rebuildShadowCache()
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) { shadowCache = {}; return; }

    shadowCache = juce::Image(juce::Image::ARGB, w, h, true);
    juce::Graphics sg(shadowCache);

    //--- panel shadows (timeline, inspector, global) ---
    eg::drawSoftShadow(sg, timelineArea.toFloat());
    eg::drawSoftShadow(sg, inspectorArea.toFloat());
    eg::drawSoftShadow(sg, globalArea.toFloat());

    //--- smaller shadows behind the guide-card tabs + undo/redo arrows ---
    auto softShadow = [&](juce::Rectangle<int> b)
    {
        juce::Path pth;
        pth.addRoundedRectangle(b.toFloat(), 8.0f);
        juce::DropShadow(juce::Colour(0x336a4f86), 12, { 0, 4 }).drawForPath(sg, pth);
        juce::DropShadow(juce::Colour(0x22473159),  5, { 0, 2 }).drawForPath(sg, pth);
    };
    for (auto& t : layerTabs)  softShadow(t.getBounds());
    for (auto& t : beatBtns)   softShadow(t.getBounds());
    for (auto& t : subdivTabs) softShadow(t.getBounds());
    softShadow(undoBtn.getBounds());
    softShadow(redoBtn.getBounds());
}
