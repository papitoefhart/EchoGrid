#include "NodeInspector.h"
#include "NodeTimeline.h"
#include "Theme.h"

//==============================================================================
// Helpers
//==============================================================================
static void styleKnob(juce::Slider& s)
{
    s.setColour(juce::Slider::rotarySliderFillColourId, eg::col::lilac);  // overridden per knob
}

static void styleButton(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonOnColourId, eg::col::lilac);
    b.setColour(juce::TextButton::textColourOffId,  eg::col::ink2);
    b.setColour(juce::TextButton::textColourOnId,   eg::col::ink);
}

//--- cheap FNV-1a folding to detect changes to what the UI shows, so the 30 Hz
//    timer only repaints when something actually moved ---
static inline void mixBits(juce::uint64& h, juce::uint32 b) noexcept { h = (h ^ b) * 1099511628211ull; }
static inline void mixF(juce::uint64& h, float f) noexcept { juce::uint32 b; std::memcpy(&b, &f, sizeof(b)); mixBits(h, b); }
static inline void mixI(juce::uint64& h, int v)   noexcept { mixBits(h, (juce::uint32)v); }

//--- IN TIME / FREE reverse-timing toggle: DISABLED in the UI for now (it added no
//    clear musical benefit).  The toggle, its DSP (the FREE mode in PluginProcessor)
//    and the reverseLock node field are all kept intact — flip this flag back to
//    true to re-expose it.  With it disabled the reverse stays in IN TIME mode
//    (attack locked to the beat); the REV LEN knob still works. ---
static constexpr bool kShowReverseTimingToggle = false;

//==============================================================================
NodeInspector::NodeInspector(EchoGridProcessor& p, NodeTimeline& t)
    : processor(p), timeline(t)
{
    //--- node label ---
    nodeLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    nodeLabel.setColour(juce::Label::textColourId, eg::col::ink);
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
            processor.dry.gain  = val;      // immediate, for the timeline's dry-node draw
            *processor.pDryGain = val;       // the parameter the host/automation owns
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
    panSlider.setColour(juce::Slider::rotarySliderFillColourId, eg::col::blue);
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
            processor.dry.pan  = val;
            *processor.pDryPan = val;
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

    //--- saturation knob (0..1): per-tap tape grit (rose) ---
    satSlider.setRange(0.0, 1.0, 0.01);
    satSlider.setDoubleClickReturnValue(true, 0.0);
    styleKnob(satSlider);
    satSlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffe07aa6));
    satSlider.onDragStart = [this]
    {
        satDragging   = true;
        timeline.captureSnapshot();
        satRefAtStart = (float)satSlider.getValue();
        satAtDragStart.clear();
        for (int i : timeline.getMultiSelection())
            if (i >= 0 && i < (int)processor.nodes.size())
                satAtDragStart.push_back(processor.nodes[i].saturation);
    };
    satSlider.onDragEnd = [this] { satDragging = false; timeline.pushUndoIfChanged(); };
    satSlider.onValueChange = [this]
    {
        if (syncing) return;
        float val = (float)satSlider.getValue();
        int   idx = timeline.getSelectedIndex();
        const auto& sel = timeline.getMultiSelection();
        const juce::ScopedLock sl(processor.getCallbackLock());
        if (sel.size() <= 1)
        {
            if (idx >= 0 && idx < (int)processor.nodes.size())
                processor.nodes[idx].saturation = val;
        }
        else
        {
            float delta = val - satRefAtStart;
            for (int k = 0; k < (int)std::min(sel.size(), satAtDragStart.size()); ++k)
                if (sel[k] >= 0 && sel[k] < (int)processor.nodes.size())
                    processor.nodes[sel[k]].saturation = juce::jlimit(0.0f, 1.0f, satAtDragStart[k] + delta);
        }
        timeline.repaint();
    };
    addAndMakeVisible(satSlider);

    //--- pitch knob (±12 semitones, whole-step snapped) ---
    pitchSlider.setRange(-12.0, 12.0, 1.0);
    pitchSlider.setDoubleClickReturnValue(true, 0.0);
    styleKnob(pitchSlider);
    pitchSlider.setColour(juce::Slider::rotarySliderFillColourId, eg::col::green);
    pitchSlider.onDragStart = [this]
    {
        pitchDragging   = true;
        timeline.captureSnapshot();
        pitchRefAtStart = (float)pitchSlider.getValue();
        pitchAtDragStart.clear();
        for (int i : timeline.getMultiSelection())
            if (i >= 0 && i < (int)processor.nodes.size())
                pitchAtDragStart.push_back(processor.nodes[i].pitchSemitones);
    };
    pitchSlider.onDragEnd = [this] { pitchDragging = false; timeline.pushUndoIfChanged(); };
    pitchSlider.onValueChange = [this]
    {
        if (syncing) return;
        float val = (float)pitchSlider.getValue();
        int   idx = timeline.getSelectedIndex();
        const auto& sel = timeline.getMultiSelection();
        const juce::ScopedLock sl(processor.getCallbackLock());
        if (sel.size() <= 1)
        {
            if (idx >= 0 && idx < (int)processor.nodes.size())
                processor.nodes[idx].pitchSemitones = val;
        }
        else
        {
            float delta = val - pitchRefAtStart;
            for (int k = 0; k < (int)std::min(sel.size(), pitchAtDragStart.size()); ++k)
                if (sel[k] >= 0 && sel[k] < (int)processor.nodes.size())
                    processor.nodes[sel[k]].pitchSemitones =
                        juce::jlimit(-12.0f, 12.0f, std::round(pitchAtDragStart[k] + delta));
        }
        timeline.repaint();
    };
    addAndMakeVisible(pitchSlider);

    //--- reverse length knob (0..1): length of the reversed segment ---
    revLenSlider.setRange(0.0, 1.0, 0.01);
    revLenSlider.setDoubleClickReturnValue(true, 1.0);
    styleKnob(revLenSlider);
    revLenSlider.setColour(juce::Slider::rotarySliderFillColourId, eg::col::pink);
    revLenSlider.onDragStart = [this]
    {
        revLenDragging   = true;
        timeline.captureSnapshot();
        revLenRefAtStart = (float)revLenSlider.getValue();
        revLenAtDragStart.clear();
        for (int i : timeline.getMultiSelection())
            if (i >= 0 && i < (int)processor.nodes.size())
                revLenAtDragStart.push_back(processor.nodes[i].reverseLength);
    };
    revLenSlider.onDragEnd = [this] { revLenDragging = false; timeline.pushUndoIfChanged(); };
    revLenSlider.onValueChange = [this]
    {
        if (syncing) return;
        float val = (float)revLenSlider.getValue();
        int   idx = timeline.getSelectedIndex();
        const auto& sel = timeline.getMultiSelection();
        const juce::ScopedLock sl(processor.getCallbackLock());
        if (sel.size() <= 1)
        {
            if (idx >= 0 && idx < (int)processor.nodes.size())
                processor.nodes[idx].reverseLength = val;
        }
        else
        {
            float delta = val - revLenRefAtStart;
            for (int k = 0; k < (int)std::min(sel.size(), revLenAtDragStart.size()); ++k)
                if (sel[k] >= 0 && sel[k] < (int)processor.nodes.size())
                    processor.nodes[sel[k]].reverseLength = juce::jlimit(0.0f, 1.0f, revLenAtDragStart[k] + delta);
        }
        timeline.repaint();
    };
    addAndMakeVisible(revLenSlider);

    //--- IN TIME / FREE toggle: whether the reverse length stays locked to the beat ---
    styleButton(revLockBtn);
    revLockBtn.setClickingTogglesState(true);
    revLockBtn.onClick = [this]
    {
        if (syncing) return;
        const auto& sel = timeline.getMultiSelection();
        if (sel.empty()) return;
        timeline.captureSnapshot();
        bool locked = revLockBtn.getToggleState();
        revLockBtn.setButtonText(locked ? "IN TIME" : "FREE");
        {
            const juce::ScopedLock sl(processor.getCallbackLock());
            for (int i : sel)
                if (i >= 0 && i < (int)processor.nodes.size())
                    processor.nodes[i].reverseLock = locked;
        }
        timeline.pushUndoIfChanged();
    };
    if (kShowReverseTimingToggle) addAndMakeVisible(revLockBtn);

    //--- reverse button (blue) ---
    styleButton(reverseBtn);
    reverseBtn.setColour(juce::TextButton::buttonOnColourId, eg::col::blue);
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

    //--- active button (lilac): applies to all selected nodes, supports undo ---
    styleButton(activeBtn);
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
    const int kw = 52;   // knob size (square)
    const int bh = 30;
    const int bw = 58;

    //--- the left ~196 px is the painted hero (big level % + chip); no child there ---
    nodeLabel.setVisible(false);   // superseded by the hero

    const int kx = 224;            // knob columns start right of hero + separator
    const int kg = 68;             // column pitch
    gainSlider.setBounds  (kx,          cy - kw / 2, kw, kw);
    panSlider.setBounds   (kx + kg,     cy - kw / 2, kw, kw);
    satSlider.setBounds   (kx + 2 * kg, cy - kw / 2, kw, kw);
    pitchSlider.setBounds (kx + 3 * kg, cy - kw / 2, kw, kw);
    revLenSlider.setBounds(kx + 4 * kg, cy - kw / 2, kw, kw);

    //--- mode buttons, right-aligned ---
    int rx = getWidth() - 16;
    activeBtn.setBounds  (rx - bw, cy - bh / 2, bw, bh);  rx -= bw + 8;
    reverseBtn.setBounds (rx - bw, cy - bh / 2, bw, bh);  rx -= bw + 8;
    if (kShowReverseTimingToggle) revLockBtn.setBounds(rx - 64, cy - bh / 2, 64, bh);
}

//==============================================================================
// Paint
//==============================================================================
void NodeInspector::paint(juce::Graphics& g)
{
    //--- dark rounded card (corners unpainted so the editor's soft shadow shows) ---
    auto panelR = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(eg::col::surface);
    g.fillRoundedRectangle(panelR, eg::kPanelRadius);
    eg::strokeCardBorder(g, panelR);

    //--- brand logo: lives in the left space beside the knobs (moved here from the
    //    editor's top-left corner), sized to fill it.  "echo" off-white, "grid" lilac. ---
    {
        juce::Font bf(32.0f, juce::Font::bold);
        g.setFont(bf);
        const int lx = 24, ly = (getHeight() - 44) / 2;   // vertically centre the logo block
        g.setColour(eg::col::brand);
        g.drawText("echo", lx, ly, 140, 36, juce::Justification::left, false);
        const int ew = (int)bf.getStringWidthFloat("echo");
        g.setColour(eg::col::lilacDeep);
        g.drawText("grid", lx + ew, ly, 140, 36, juce::Justification::left, false);

        g.setColour(eg::col::ink3);
        g.setFont(juce::Font(9.0f));
        g.drawText("MULTI-TAP DELAY   v" + eg::kVersionLabel + " ALPHA", lx + 2, ly + 35, 220, 12,
                   juce::Justification::left, false);
    }

    //--- separator between the brand and the controls (always shown) ---
    g.setColour(eg::col::line);
    g.drawLine(206.0f, 18.0f, 206.0f, (float)getHeight() - 18.0f, 1.0f);

    int idx = timeline.getSelectedIndex();

    //--- nothing selected: the dimmed knobs already read as "inactive", so just stop
    //    here (no hint text — it would collide with the ghosted knobs) ---
    if (idx == -1)
        return;

    bool isEcho = (idx >= 0 && idx < (int)processor.nodes.size());
    bool isRev  = (isEcho && processor.nodes[idx].reverse);

    //--- section labels above each control ---
    g.setColour(eg::col::ink3);
    g.setFont(9.0f);

    auto labelAbove = [&](juce::Rectangle<int> r, const juce::String& text)
    {
        g.drawText(text, r.withY(r.getY() - 16).withHeight(13),
                   juce::Justification::centred);
    };

    labelAbove(gainSlider.getBounds(), "LEVEL");
    labelAbove(panSlider.getBounds(),  "PAN");
    if (isEcho) { labelAbove(satSlider.getBounds(),   "SAT");
                  labelAbove(pitchSlider.getBounds(), "PITCH"); }
    if (isRev)    labelAbove(revLenSlider.getBounds(), "REV LEN");
    if (isRev && kShowReverseTimingToggle) labelAbove(revLockBtn.getBounds(), "TIMING");
    labelAbove(reverseBtn.getBounds(),  "MODE");

    //--- value readouts below each knob ---
    g.setColour(eg::col::ink2);
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

    if (isEcho)
    {
        valueBelow(satSlider.getBounds(), juce::String((float)satSlider.getValue(), 2));

        int st = (int)std::round((float)pitchSlider.getValue());
        juce::String pitchStr = (st == 0) ? "0" : (st > 0 ? "+" : "") + juce::String(st);
        valueBelow(pitchSlider.getBounds(), pitchStr);
    }

    if (isRev)
        valueBelow(revLenSlider.getBounds(),
                   juce::String((int)std::round((float)revLenSlider.getValue() * 100.0f)) + "%");
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

    //--- the reverse-shaping controls only matter when this node is in reverse mode ---
    bool isRev = isEcho && processor.nodes[idx].reverse;

    //--- show/hide ---
    gainSlider.setVisible(isDry || isEcho);
    panSlider.setVisible(isDry || isEcho);
    satSlider.setVisible(isEcho);
    pitchSlider.setVisible(isEcho);
    revLenSlider.setVisible(isRev);
    revLockBtn.setVisible(isRev && kShowReverseTimingToggle);
    reverseBtn.setVisible(isEcho);
    activeBtn.setVisible(isEcho);
    nodeLabel.setVisible(false);   // superseded by the painted hero

    syncing = true;

    if (isDry)
    {
        nodeLabel.setText("DRY", juce::dontSendNotification);
        gainSlider.setValue(processor.pDryGain->get(), juce::dontSendNotification);
        panSlider.setValue(processor.pDryPan->get(),   juce::dontSendNotification);
    }
    else if (isEcho)
    {
        const auto& n = processor.nodes[idx];
        nodeLabel.setText("@ " + juce::String(n.positionBeats + 1.0f, 2) + "b",
                          juce::dontSendNotification);
        if (!gainDragging)  gainSlider.setValue(n.gain, juce::dontSendNotification);
        if (!panDragging)   panSlider.setValue(n.pan,  juce::dontSendNotification);
        if (!satDragging)   satSlider.setValue(n.saturation, juce::dontSendNotification);
        if (!pitchDragging) pitchSlider.setValue(n.pitchSemitones, juce::dontSendNotification);
        if (!revLenDragging) revLenSlider.setValue(n.reverseLength, juce::dontSendNotification);
        revLockBtn.setToggleState(n.reverseLock, juce::dontSendNotification);
        revLockBtn.setButtonText(n.reverseLock ? "IN TIME" : "FREE");
        reverseBtn.setToggleState(n.reverse, juce::dontSendNotification);
        activeBtn.setToggleState(n.active,   juce::dontSendNotification);
    }

    syncing = false;

    //--- repaint only on real change.  This used to call repaint() + timeline.repaint()
    //    unconditionally every tick (30x/sec), saturating the message thread that also
    //    handles mouse clicks — the main cause of the sluggish feel. ---
    juce::uint64 isig = inspectorSig(idx);
    if (isig != lastInspectorSig) { lastInspectorSig = isig; repaint(); }

    //--- the timeline draws all node data; repaint it only when that data changed
    //    (e.g. host loaded state, or a value moved elsewhere) rather than continuously ---
    juce::uint64 msig = modelSig();
    if (msig != lastModelSig) { lastModelSig = msig; timeline.repaint(); }
}

//==============================================================================
// Change-detection signatures — fold the state each view draws into one integer
//==============================================================================
juce::uint64 NodeInspector::inspectorSig(int idx) const
{
    juce::uint64 h = 1469598103934665603ull;
    mixI(h, idx);
    if (idx == -2)
    {
        mixF(h, processor.dry.gain);
        mixF(h, processor.dry.pan);
    }
    else if (idx >= 0 && idx < (int)processor.nodes.size())
    {
        const auto& n = processor.nodes[idx];
        mixF(h, n.positionBeats); mixF(h, n.gain);           mixF(h, n.pan);
        mixF(h, n.saturation);    mixF(h, n.pitchSemitones); mixF(h, n.reverseLength);
        mixI(h, n.reverse ? 1 : 0); mixI(h, n.active ? 1 : 0); mixI(h, n.reverseLock ? 1 : 0);
    }
    return h;
}

juce::uint64 NodeInspector::modelSig() const
{
    juce::uint64 h = 1469598103934665603ull;
    mixF(h, processor.dry.gain);        mixF(h, processor.dry.pan);
    mixF(h, processor.gridLengthBeats); mixF(h, processor.snapStepBeats);
    mixI(h, (int)processor.nodes.size());
    for (const auto& n : processor.nodes)
    {
        mixF(h, n.positionBeats); mixF(h, n.gain);           mixF(h, n.pan);
        mixF(h, n.saturation);    mixF(h, n.pitchSemitones); mixF(h, n.reverseLength);
        mixF(h, n.probability);
        mixI(h, n.reverse ? 1 : 0); mixI(h, n.active ? 1 : 0); mixI(h, n.reverseLock ? 1 : 0);
    }
    return h;
}
