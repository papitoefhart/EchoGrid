#include "NodeTimeline.h"

//==============================================================================
// Helpers
//==============================================================================
static juce::Colour panToColour(float pan)
{
    const juce::Colour left  (0xff5599ff);
    const juce::Colour centre(0xffe0e0ff);
    const juce::Colour right (0xffff9944);
    return pan < 0.0f ? centre.interpolatedWith(left,  -pan)
                      : centre.interpolatedWith(right,  pan);
}

//==============================================================================
namespace {
struct NodeAction : public juce::UndoableAction
{
    using State = NodeTimeline::EditorState;
    EchoGridProcessor& proc;
    State before, after;

    NodeAction(EchoGridProcessor& p, State b, State a)
        : proc(p), before(std::move(b)), after(std::move(a)) {}

    bool perform() override
    {
        const juce::ScopedLock sl(proc.getCallbackLock());
        proc.nodes           = after.nodes;
        proc.dry             = after.dry;
        proc.gridLengthBeats = after.gridLengthBeats;
        proc.subdivisions    = after.subdivisions;
        return true;
    }

    bool undo() override
    {
        const juce::ScopedLock sl(proc.getCallbackLock());
        proc.nodes           = before.nodes;
        proc.dry             = before.dry;
        proc.gridLengthBeats = before.gridLengthBeats;
        proc.subdivisions    = before.subdivisions;
        return true;
    }
};
}

NodeTimeline::NodeTimeline(EchoGridProcessor& p, juce::UndoManager& um)
    : processor(p), undoManager(um)
{
    setWantsKeyboardFocus(true);
}

void NodeTimeline::captureSnapshot()
{
    snapshotBefore.nodes           = processor.nodes;
    snapshotBefore.dry             = processor.dry;
    snapshotBefore.gridLengthBeats = processor.gridLengthBeats;
    snapshotBefore.subdivisions    = processor.subdivisions;
}

void NodeTimeline::pushUndoIfChanged()
{
    EditorState current { processor.nodes, processor.dry,
                          processor.gridLengthBeats, processor.subdivisions };

    //--- bit-exact comparisons to detect any change since captureSnapshot() ---
    bool changed = current.nodes != snapshotBefore.nodes
        || std::memcmp(&current.dry,             &snapshotBefore.dry,             sizeof(DryNode)) != 0
        || std::memcmp(&current.gridLengthBeats, &snapshotBefore.gridLengthBeats, sizeof(float))   != 0
        || current.subdivisions != snapshotBefore.subdivisions;

    if (changed)
        undoManager.perform(new NodeAction(processor, snapshotBefore, current));

    repaint();
}

//==============================================================================
// Cursor + tooltip helpers
//==============================================================================
void NodeTimeline::updateCursor(juce::Point<float> pos, bool /*altDown*/)
{
    if (dryNodeAt(pos) || echoNodeAt(pos) >= 0)
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void NodeTimeline::timerCallback()
{
    showProbTooltip = false;
    stopTimer();
    repaint();
}

//==============================================================================
// Coordinate helpers
//  X: kPadX (left edge = beat 0 / dry) … width-kPadX (right edge = processor.gridLengthBeats)
//  Y: kPadY (top = gain 1.0)           … height-kPadY (bottom = gain 0.0)
//==============================================================================
float NodeTimeline::beatToX(float beat) const
{
    float usable = (float)getWidth() - 2.0f * kPadX;
    return kPadX + beat / processor.gridLengthBeats * usable;
}

//--- gain curve: cubic with minimum slope at fader=0.35 (gain=0.20) ---
//    most fader travel (~48%) dedicated to the 0.10–0.30 gain range;
//    the 0.30–1.0 range is compressed into the top ~40%.
//    gain = A·f³ + B·f² + C·f  (A+B+C=1, always monotonic)
static constexpr float kGA =  2.198f;
static constexpr float kGB = -2.308f;
static constexpr float kGC =  1.110f;

static float faderToGain(float f)
{
    return kGA * f*f*f + kGB * f*f + kGC * f;
}

static float gainToFader(float gain)
{
    float f = gain; // initial guess
    for (int i = 0; i < 8; ++i)
    {
        float g  = faderToGain(f);
        float gp = 3.0f*kGA*f*f + 2.0f*kGB*f + kGC;
        if (std::abs(gp) < 1e-6f) break;
        f -= (g - gain) / gp;
        f  = juce::jlimit(0.0f, 1.0f, f);
    }
    return f;
}

float NodeTimeline::gainToY(float gain) const
{
    float usable = (float)getHeight() - 2.0f * kPadY;
    float fader  = gainToFader(juce::jlimit(0.0f, 1.0f, gain));
    return kPadY + (1.0f - fader) * usable;
}

float NodeTimeline::xToBeat(float x) const
{
    float usable = (float)getWidth() - 2.0f * kPadX;
    return (x - kPadX) / usable * processor.gridLengthBeats;
}

float NodeTimeline::yToGain(float y) const
{
    float usable = (float)getHeight() - 2.0f * kPadY;
    float fader  = juce::jlimit(0.0f, 1.0f, 1.0f - (y - kPadY) / usable);
    return faderToGain(fader);
}

float NodeTimeline::panToY(float pan) const
{
    //--- top = L (-1), bottom = R (+1) ---
    float usable = (float)getHeight() - 2.0f * kPadY;
    return kPadY + (pan + 1.0f) * 0.5f * usable;
}

float NodeTimeline::yToPan(float y) const
{
    float usable = (float)getHeight() - 2.0f * kPadY;
    return juce::jlimit(-1.0f, 1.0f, (y - kPadY) / usable * 2.0f - 1.0f);
}

int NodeTimeline::panNodeAt(juce::Point<float> p) const
{
    //--- match by X proximity only so a click anywhere in a node's vertical
    //    column counts as a hit — the Y position sets the pan value directly ---
    const float xThresh = kNodeRadius + 4.0f;
    int   bestIdx  = -1;
    float bestDist = xThresh + 1.0f;

    const auto& nodes = processor.nodes;
    for (int i = 0; i < (int)nodes.size(); ++i)
    {
        float dx = std::abs(p.x - beatToX(nodes[i].positionBeats));
        if (dx <= xThresh && dx < bestDist) { bestDist = dx; bestIdx = i; }
    }
    return bestIdx;
}

void NodeTimeline::placePanLineBetween(juce::Point<float> from, juce::Point<float> to)
{
    float xA    = std::min(from.x, to.x);
    float xB    = std::max(from.x, to.x);
    float xSpan = to.x - from.x;

    const juce::ScopedLock sl(processor.getCallbackLock());
    for (auto& n : processor.nodes)
    {
        float nx = beatToX(n.positionBeats);
        if (nx < xA - kNodeRadius || nx > xB + kNodeRadius) continue;
        float pan;
        if (std::abs(xSpan) < 1.0f)
            pan = yToPan((from.y + to.y) * 0.5f);
        else {
            float t = juce::jlimit(0.0f, 1.0f, (nx - from.x) / xSpan);
            pan = yToPan(from.y + t * (to.y - from.y));
        }
        n.pan = pan;
    }
}

float NodeTimeline::snapBeat(float beat) const
{
    float step = processor.gridLengthBeats / (float)processor.subdivisions;
    return std::round(beat / step) * step;
}

//==============================================================================
// Hit testing
//==============================================================================
bool NodeTimeline::dryNodeAt(juce::Point<float> p) const
{
    float dx = beatToX(0.0f);
    float dy = gainToY(processor.dry.gain);
    return p.getDistanceFrom({ dx, dy }) <= kNodeRadius + 4.0f;
}

int NodeTimeline::echoNodeAt(juce::Point<float> p) const
{
    const auto& nodes = processor.nodes;
    for (int i = (int)nodes.size() - 1; i >= 0; --i)
    {
        float nx = beatToX(nodes[i].positionBeats);
        float ny = gainToY(nodes[i].gain);
        if (p.getDistanceFrom({ nx, ny }) <= kNodeRadius + 4.0f)
            return i;
    }
    return -1;
}

//==============================================================================
// Paint
//==============================================================================
void NodeTimeline::paint(juce::Graphics& g)
{
    const float w = (float)getWidth();
    const float h = (float)getHeight();

    //--- background ---
    g.fillAll(juce::Colour(0xff141422));

    //--- grid lines ---
    const float stepBeats    = processor.gridLengthBeats / (float)processor.subdivisions;
    const int   stepsPerBeat = processor.subdivisions / (int)processor.gridLengthBeats;

    for (int i = 0; i <= processor.subdivisions; ++i)
    {
        float x      = beatToX(i * stepBeats);
        bool  isBeat = (i % stepsPerBeat) == 0;
        g.setColour(isBeat ? juce::Colour(0xff3a3a5a) : juce::Colour(0xff22223a));
        g.drawVerticalLine((int)x, kPadY * 0.5f, h - kPadY * 0.5f);
    }

    //--- labels: DRY at beat 0, then 1 2 3 4 ---
    g.setFont(10.0f);
    g.setColour(juce::Colour(0xff5555aa));
    g.drawText("DRY", (int)beatToX(0.0f) - 12, 4, 24, 12,
               juce::Justification::centred, false);
    for (int b = 1; b <= (int)processor.gridLengthBeats; ++b)
        g.drawText(juce::String(b), (int)beatToX((float)b) - 8, 4, 16, 12,
                   juce::Justification::centred, false);

    //--- bottom baseline ---
    g.setColour(juce::Colour(0xff3a3a5a));
    g.drawHorizontalLine((int)(h - kPadY * 0.5f), 0.0f, w);

    //--- helper: draw one node ---
    //    inactive nodes are rendered grey + dimmed so they read as "off"
    //    reversed nodes show a left-pointing arrow overlay
    auto drawNode = [&](float x, float y, float pan, float probability,
                        bool isReversed, bool active, bool selected)
    {
        auto  c     = active ? panToColour(pan) : juce::Colour(0xff555568);
        float alpha = active ? juce::jmap(probability, 0.0f, 1.0f, 0.15f, 1.0f) : 0.30f;

        g.setColour(c.withAlpha(0.25f * alpha));
        g.drawLine(x, y + kNodeRadius, x, h - kPadY * 0.5f, active ? 1.5f : 0.8f);

        g.setColour(c.withAlpha(0.20f * alpha));
        float gr = kNodeRadius + 5.0f;
        g.fillEllipse(x - gr, y - gr, gr * 2.0f, gr * 2.0f);

        g.setColour(c.withAlpha(alpha));
        g.fillEllipse(x - kNodeRadius, y - kNodeRadius,
                      kNodeRadius * 2.0f, kNodeRadius * 2.0f);

        {
            //--- direction arrow: ◄ for reverse, ► for forward ---
            juce::Path arrow;
            if (isReversed)
                arrow.addTriangle(x + 4.0f, y - 4.5f, x - 3.5f, y, x + 4.0f, y + 4.5f);
            else
                arrow.addTriangle(x - 4.0f, y - 4.5f, x + 3.5f, y, x - 4.0f, y + 4.5f);
            g.setColour(juce::Colour(0xff141422).withAlpha(alpha));
            g.fillPath(arrow);
        }

        if (selected)
        {
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawEllipse(x - kNodeRadius - 2.0f, y - kNodeRadius - 2.0f,
                          (kNodeRadius + 2.0f) * 2.0f, (kNodeRadius + 2.0f) * 2.0f, 1.5f);
        }
    };

    //--- dry node ---
    {
        float x = beatToX(0.0f);
        float y = gainToY(processor.dry.gain);
        drawNode(x, y, processor.dry.pan, 1.0f, false, true, selectedIndex == -2);

        g.setColour(juce::Colour(0xff141422));
        g.fillRect(x - 3.5f, y - 3.5f, 7.0f, 7.0f);
        g.setColour(panToColour(processor.dry.pan));
        g.drawRect(x - 3.5f, y - 3.5f, 7.0f, 7.0f, 1.5f);
    }

    const auto& nodes = processor.nodes;

    //--- echo nodes ---
    for (int i = 0; i < (int)nodes.size(); ++i)
    {
        bool inSel = std::find(multiSelection.begin(), multiSelection.end(), i)
                     != multiSelection.end();
        drawNode(beatToX(nodes[i].positionBeats),
                 gainToY(nodes[i].gain),
                 nodes[i].pan,
                 nodes[i].probability,
                 nodes[i].reverse,
                 nodes[i].active,
                 inSel);
    }

    //--- beat-position labels above each echo node ---
    g.setFont(8.5f);
    for (int i = 0; i < (int)nodes.size(); ++i)
    {
        float nx = beatToX(nodes[i].positionBeats);
        float ny = gainToY(nodes[i].gain);
        g.setColour(juce::Colour(0xff6677aa).withAlpha(nodes[i].active ? 0.9f : 0.4f));
        g.drawText(juce::String(nodes[i].positionBeats, 2),
                   (int)nx - 18, (int)(ny - kNodeRadius - 14), 36, 11,
                   juce::Justification::centred, false);
    }

    //--- pan edit overlay ---
    if (panEditMode)
    {
        //--- dim the gain layer ---
        g.setColour(juce::Colour(0x99000000));
        g.fillRect(0.0f, 0.0f, w, h);

        //--- center line (pan = 0) ---
        float panCy = panToY(0.0f);
        g.setColour(juce::Colour(0xff3a3a5a));
        g.drawHorizontalLine((int)panCy, kPadX, w - kPadX);

        //--- L / R axis labels ---
        g.setFont(9.0f);
        g.setColour(juce::Colour(0xff5566aa));
        g.drawText("L", 4, (int)panToY(-1.0f) - 6, 14, 12, juce::Justification::centred, false);
        g.drawText("R", 4, (int)panToY( 1.0f) - 6, 14, 12, juce::Justification::centred, false);

        //--- echo node pan dots ---
        for (int i = 0; i < (int)nodes.size(); ++i)
        {
            float nx   = beatToX(nodes[i].positionBeats);
            float ny   = panToY(nodes[i].pan);
            float alpha = nodes[i].active ? 0.9f : 0.4f;
            auto  c    = panToColour(nodes[i].pan);
            bool  inSel = std::find(multiSelection.begin(), multiSelection.end(), i)
                          != multiSelection.end();

            g.setColour(c.withAlpha(0.35f * alpha));
            g.drawLine(nx, panCy, nx, ny, 2.0f);
            g.setColour(c.withAlpha(alpha));
            g.fillEllipse(nx - kNodeRadius, ny - kNodeRadius, kNodeRadius * 2.0f, kNodeRadius * 2.0f);
            if (inSel)
            {
                g.setColour(juce::Colours::white.withAlpha(0.8f));
                g.drawEllipse(nx - kNodeRadius - 2.0f, ny - kNodeRadius - 2.0f,
                              (kNodeRadius + 2.0f) * 2.0f, (kNodeRadius + 2.0f) * 2.0f, 1.5f);
            }
        }

        //--- dry node pan dot ---
        {
            float nx = beatToX(0.0f);
            float ny = panToY(processor.dry.pan);
            auto  c  = panToColour(processor.dry.pan);
            g.setColour(c.withAlpha(0.35f));
            g.drawLine(nx, panCy, nx, ny, 2.0f);
            g.setColour(c.withAlpha(0.85f));
            g.fillEllipse(nx - kNodeRadius, ny - kNodeRadius, kNodeRadius * 2.0f, kNodeRadius * 2.0f);
            g.setColour(juce::Colour(0xff141422));
            g.fillRect(nx - 3.5f, ny - 3.5f, 7.0f, 7.0f);
            g.setColour(c.withAlpha(0.85f));
            g.drawRect(nx - 3.5f, ny - 3.5f, 7.0f, 7.0f, 1.5f);
        }

        //--- mode label ---
        g.setFont(9.0f);
        g.setColour(juce::Colour(0xff7788cc).withAlpha(0.7f));
        g.drawText("PAN EDIT", (int)(w - 62.0f), 6, 56, 10, juce::Justification::centredRight, false);
    }

    //--- rubber-band rectangle during Shift+drag ---
    if (shiftSelecting)
    {
        juce::Rectangle<float> selRect(
            std::min(shiftSelectStart.x, shiftSelectCurrent.x),
            std::min(shiftSelectStart.y, shiftSelectCurrent.y),
            std::abs(shiftSelectCurrent.x - shiftSelectStart.x),
            std::abs(shiftSelectCurrent.y - shiftSelectStart.y));
        g.setColour(juce::Colour(0xff4455bb).withAlpha(0.12f));
        g.fillRect(selRect);
        g.setColour(juce::Colour(0xff7788dd).withAlpha(0.7f));
        g.drawRect(selRect, 1.5f);
    }

    //--- probability scroll tooltip ---
    if (showProbTooltip && probTooltipIdx >= 0 && probTooltipIdx < (int)nodes.size())
    {
        float nx  = beatToX(nodes[probTooltipIdx].positionBeats);
        float ny  = gainToY(nodes[probTooltipIdx].gain);
        auto  str = juce::String((int)std::round(nodes[probTooltipIdx].probability * 100)) + "%";

        juce::Rectangle<float> pill(nx - 15.0f, ny - kNodeRadius - 27.0f, 32.0f, 16.0f);
        g.setColour(juce::Colour(0xff252540));
        g.fillRoundedRectangle(pill, 4.0f);
        g.setColour(juce::Colour(0xff99aadd));
        g.setFont(9.0f);
        g.drawText(str, pill.toNearestInt(), juce::Justification::centred, false);
    }
}

//==============================================================================
// Line draw
//==============================================================================
void NodeTimeline::placeLineBetween(juce::Point<float> from, juce::Point<float> to)
{
    const float minBeat = processor.gridLengthBeats / (float)processor.subdivisions;
    const float step    = minBeat;

    //--- collect all snap beats that fall between the two X positions ---
    float xA = std::min(from.x, to.x);
    float xB = std::max(from.x, to.x);

    float beatA = snapBeat(xToBeat(xA));
    float beatB = snapBeat(xToBeat(xB));

    //--- ensure at least the single snap position under the cursor is covered ---
    if (beatA > beatB) std::swap(beatA, beatB);
    beatA = juce::jlimit(minBeat, processor.gridLengthBeats, beatA);
    beatB = juce::jlimit(minBeat, processor.gridLengthBeats, beatB);

    const juce::ScopedLock sl(processor.getCallbackLock());

    for (float beat = beatA; beat <= beatB + step * 0.01f; beat += step)
    {
        float snapped = snapBeat(beat);
        snapped = juce::jlimit(minBeat, processor.gridLengthBeats, snapped);

        //--- interpolate gain between the two endpoints ---
        float gain;
        float xSpan = to.x - from.x;
        if (std::abs(xSpan) < 1.0f)
        {
            gain = yToGain((from.y + to.y) * 0.5f);
        }
        else
        {
            float t = (beatToX(snapped) - from.x) / xSpan;
            t       = juce::jlimit(0.0f, 1.0f, t);
            gain    = yToGain(from.y + t * (to.y - from.y));
        }
        gain = juce::jlimit(0.0f, 1.0f, gain);

        //--- update existing node or create new one ---
        bool found = false;
        for (auto& n : processor.nodes)
        {
            if (std::abs(n.positionBeats - snapped) < step * 0.01f)
            {
                n.gain = gain;
                found  = true;
                break;
            }
        }
        if (!found)
        {
            EchoNode n;
            n.positionBeats = snapped;
            n.gain          = gain;
            processor.nodes.push_back(n);
        }
    }
}

//==============================================================================
//==============================================================================
// Mouse
//==============================================================================
void NodeTimeline::mouseMove(const juce::MouseEvent& e)
{
    updateCursor(e.position, e.mods.isAltDown());
}

void NodeTimeline::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    captureSnapshot();

    //--- pan edit mode: Option = line-draw pan, Shift = select, normal = drag ---
    if (panEditMode && !e.mods.isRightButtonDown())
    {
        int panHit = panNodeAt(e.position);

        if (e.mods.isAltDown())
        {
            panLineDrawing = true;
            panLastLinePos = e.position;
            placePanLineBetween(e.position, e.position);
            repaint();
            return;
        }

        if (e.mods.isShiftDown())
        {
            if (panHit >= 0)
            {
                auto it = std::find(multiSelection.begin(), multiSelection.end(), panHit);
                if (it != multiSelection.end()) { multiSelection.erase(it); selectedIndex = multiSelection.empty() ? -1 : multiSelection.back(); }
                else { multiSelection.push_back(panHit); selectedIndex = panHit; }
            }
            else
            {
                shiftSelecting = true;
                shiftSelectStart = shiftSelectCurrent = e.position;
                multiSelection.clear();
                selectedIndex = -1;
            }
            repaint();
            return;
        }

        if (panHit >= 0)
        {
            bool inSel = std::find(multiSelection.begin(), multiSelection.end(), panHit) != multiSelection.end();
            if (!inSel) { multiSelection = { panHit }; selectedIndex = panHit; }

            //--- immediately apply the click's Y position as pan —
            //    the user doesn't need to drag; a single click sets the value ---
            float clickPan = yToPan(e.position.y);
            {
                const juce::ScopedLock sl(processor.getCallbackLock());
                processor.nodes[panHit].pan = clickPan;
            }

            //--- anchor for subsequent drag (delta from here) ---
            panDragIdx      = panHit;
            panDragStartPan = clickPan;
            multiPanAtDragStart.clear();
            for (int i : multiSelection)
                if (i >= 0 && i < (int)processor.nodes.size())
                    multiPanAtDragStart.push_back(processor.nodes[i].pan);
        }
        else { multiSelection.clear(); selectedIndex = -1; }
        repaint();
        return;
    }

    //--- Option + click: enter line-draw mode ---
    if (e.mods.isAltDown() && !e.mods.isRightButtonDown())
    {
        drawingLine = true;
        lastLinePos = e.position;
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        placeLineBetween(e.position, e.position);
        repaint();
        return;
    }

    //--- dry node ---
    if (dryNodeAt(e.position))
    {
        if (!e.mods.isRightButtonDown())
        {
            multiSelection.clear();
            dragIndex     = -2;
            selectedIndex = -2;
            dragStartPos  = { beatToX(0.0f), gainToY(processor.dry.gain) };
        }
        return;
    }

    int hit = echoNodeAt(e.position);

    //--- right-click: delete hit node (or all selected if hit is among them) ---
    if (e.mods.isRightButtonDown())
    {
        if (hit >= 0)
        {
            bool hitInSel = std::find(multiSelection.begin(), multiSelection.end(), hit)
                            != multiSelection.end();
            std::vector<int> toDelete = (hitInSel && multiSelection.size() > 1)
                                        ? multiSelection : std::vector<int>{ hit };
            std::sort(toDelete.begin(), toDelete.end(), std::greater<int>());
            const juce::ScopedLock sl(processor.getCallbackLock());
            for (int idx : toDelete)
                processor.nodes.erase(processor.nodes.begin() + idx);
            multiSelection.clear();
            selectedIndex = -1;
            repaint();
        }
        return;
    }

    //--- Shift+click on a node: toggle it in the selection ---
    //--- Shift+drag on empty space: rubber-band ---
    if (e.mods.isShiftDown() && !e.mods.isRightButtonDown())
    {
        if (hit >= 0)
        {
            auto it = std::find(multiSelection.begin(), multiSelection.end(), hit);
            if (it != multiSelection.end())
            {
                multiSelection.erase(it);
                selectedIndex = multiSelection.empty() ? -1 : multiSelection.back();
            }
            else
            {
                multiSelection.push_back(hit);
                selectedIndex = hit;
            }
        }
        else
        {
            shiftSelecting     = true;
            shiftSelectStart   = e.position;
            shiftSelectCurrent = e.position;
            multiSelection.clear();
            selectedIndex = -1;
        }
        repaint();
        return;
    }

    //--- normal click on an existing node ---
    if (hit >= 0)
    {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        dragIndex     = hit;
        selectedIndex = hit;

        //--- if clicking a node already in the selection, keep the full selection for multi-drag ---
        bool alreadyInSel = std::find(multiSelection.begin(), multiSelection.end(), hit)
                            != multiSelection.end();
        if (!alreadyInSel)
            multiSelection = { hit };

        dragStartBeat = processor.nodes[hit].positionBeats;
        dragStartPos  = { beatToX(dragStartBeat), gainToY(processor.nodes[hit].gain) };

        //--- snapshot gains of all selected nodes for relative drag ---
        multiGainAtDragStart.clear();
        for (int i : multiSelection)
            if (i >= 0 && i < (int)processor.nodes.size())
                multiGainAtDragStart.push_back(processor.nodes[i].gain);
    }
    else
    {
        //--- empty space: clear selection, snap to grid ---
        multiSelection.clear();
        selectedIndex = -1;

        float minBeat = processor.gridLengthBeats / (float)processor.subdivisions;
        float beat    = juce::jlimit(minBeat, processor.gridLengthBeats, snapBeat(xToBeat(e.position.x)));
        float gain    = juce::jlimit(0.0f, 1.0f, yToGain(e.position.y));

        for (int i = 0; i < (int)processor.nodes.size(); ++i)
        {
            if (std::abs(processor.nodes[i].positionBeats - beat) < 0.001f)
            {
                multiSelection = { i };
                dragIndex      = i;
                selectedIndex  = i;
                dragStartBeat  = processor.nodes[i].positionBeats;
                dragStartPos   = { beatToX(dragStartBeat), gainToY(processor.nodes[i].gain) };
                return;
            }
        }

        EchoNode node;
        node.positionBeats = beat;
        node.gain          = gain;
        const juce::ScopedLock sl(processor.getCallbackLock());
        processor.nodes.push_back(node);
        int newIdx     = (int)processor.nodes.size() - 1;
        multiSelection = { newIdx };
        dragIndex      = newIdx;
        selectedIndex  = newIdx;
        dragStartBeat  = beat;
        dragStartPos   = { beatToX(beat), gainToY(gain) };
        repaint();
    }
}

void NodeTimeline::mouseDrag(const juce::MouseEvent& e)
{
    //--- rubber-band runs in both modes; use pan positions when in pan edit mode ---
    if (shiftSelecting)
    {
        shiftSelectCurrent = e.position;
        juce::Rectangle<float> selRect(
            std::min(shiftSelectStart.x, shiftSelectCurrent.x),
            std::min(shiftSelectStart.y, shiftSelectCurrent.y),
            std::abs(shiftSelectCurrent.x - shiftSelectStart.x),
            std::abs(shiftSelectCurrent.y - shiftSelectStart.y));
        multiSelection.clear();
        const auto& ns = processor.nodes;
        for (int i = 0; i < (int)ns.size(); ++i)
        {
            float ny = panEditMode ? panToY(ns[i].pan) : gainToY(ns[i].gain);
            if (selRect.contains(beatToX(ns[i].positionBeats), ny))
                multiSelection.push_back(i);
        }
        selectedIndex = multiSelection.empty() ? -1 : multiSelection.back();
        repaint();
        return;
    }

    //--- pan line draw (Option+drag in pan mode) ---
    if (panEditMode && panLineDrawing)
    {
        placePanLineBetween(panLastLinePos, e.position);
        panLastLinePos = e.position;
        repaint();
        return;
    }

    //--- pan dot drag ---
    if (panEditMode)
    {
        if (panDragIdx >= 0 && panDragIdx < (int)processor.nodes.size())
        {
            float pan   = yToPan(e.position.y);
            bool  inSel = std::find(multiSelection.begin(), multiSelection.end(), panDragIdx)
                          != multiSelection.end();
            const juce::ScopedLock sl(processor.getCallbackLock());
            if (inSel && multiSelection.size() > 1 && !multiPanAtDragStart.empty())
            {
                float delta = pan - panDragStartPan;
                for (int k = 0; k < (int)std::min(multiSelection.size(), multiPanAtDragStart.size()); ++k)
                    if (multiSelection[k] >= 0 && multiSelection[k] < (int)processor.nodes.size())
                        processor.nodes[multiSelection[k]].pan =
                            juce::jlimit(-1.0f, 1.0f, multiPanAtDragStart[k] + delta);
            }
            else
            {
                processor.nodes[panDragIdx].pan = pan;
            }
            repaint();
        }
        return;
    }

    //--- line-draw mode: fill snap positions as mouse sweeps ---
    if (drawingLine)
    {
        placeLineBetween(lastLinePos, e.position);
        lastLinePos = e.position;
        repaint();
        return;
    }

    //--- dry node: only gain changes (locked to beat 0) ---
    if (dragIndex == -2)
    {
        processor.dry.gain = yToGain(e.position.y);
        repaint();
        return;
    }

    if (dragIndex < 0 || dragIndex >= (int)processor.nodes.size()) return;

    float beat = snapBeat(xToBeat(e.position.x));
    float minBeat = processor.gridLengthBeats / (float)processor.subdivisions;
    beat = juce::jlimit(minBeat, processor.gridLengthBeats, beat);
    float gain = yToGain(e.position.y);

    //--- block landing on a step already occupied by another node ---
    bool beatOccupied = false;
    for (int i = 0; i < (int)processor.nodes.size(); ++i)
    {
        if (i != dragIndex && std::abs(processor.nodes[i].positionBeats - beat) < 0.001f)
        {
            beatOccupied = true;
            break;
        }
    }

    {
        const juce::ScopedLock sl(processor.getCallbackLock());
        if (!beatOccupied)
            processor.nodes[dragIndex].positionBeats = beat;

        if (multiSelection.size() > 1 && !multiGainAtDragStart.empty())
        {
            //--- delta gain: shift all selected nodes by the same amount.
            //    dragStartPos.y = gainToY(dragged node's gain at mouseDown),
            //    so yToGain(dragStartPos.y) recovers the original gain exactly. ---
            float delta = gain - yToGain(dragStartPos.y);
            for (int k = 0; k < (int)std::min(multiSelection.size(), multiGainAtDragStart.size()); ++k)
            {
                int i = multiSelection[k];
                if (i >= 0 && i < (int)processor.nodes.size())
                    processor.nodes[i].gain = juce::jlimit(0.0f, 1.0f, multiGainAtDragStart[k] + delta);
            }
        }
        else
        {
            processor.nodes[dragIndex].gain = gain;
        }
    }
    repaint();
}

void NodeTimeline::mouseUp(const juce::MouseEvent& e)
{
    drawingLine          = false;
    shiftSelecting       = false;
    dragIndex            = -1;
    panDragIdx           = -1;
    panLineDrawing       = false;
    multiGainAtDragStart.clear();
    multiPanAtDragStart.clear();
    pushUndoIfChanged();
    updateCursor(e.position, e.mods.isAltDown());
}

void NodeTimeline::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (dryNodeAt(e.position)) return; // dry node has no reverse

    int hit = echoNodeAt(e.position);
    if (hit >= 0)
    {
        const juce::ScopedLock sl(processor.getCallbackLock());
        processor.nodes[hit].reverse = !processor.nodes[hit].reverse;
        repaint();
    }
}

bool NodeTimeline::keyPressed(const juce::KeyPress& key)
{
    //--- Cmd+Z / Cmd+Shift+Z / Cmd+Y ---
    if (key.getModifiers().isCommandDown())
    {
        if (key.getKeyCode() == 'z')
        {
            if (key.getModifiers().isShiftDown()) undoManager.redo();
            else                                  undoManager.undo();
            repaint();
            return true;
        }
        if (key.getKeyCode() == 'y') { undoManager.redo(); repaint(); return true; }
    }

    bool isDelete = key == juce::KeyPress::backspaceKey
                 || key == juce::KeyPress::deleteKey;
    if (!isDelete) return false;
    if (selectedIndex == -2) return true; // dry node protected

    if (multiSelection.empty()) return false;

    //--- delete all selected nodes (highest index first to avoid shifting) ---
    std::vector<int> toDelete = multiSelection;
    std::sort(toDelete.begin(), toDelete.end(), std::greater<int>());
    {
        const juce::ScopedLock sl(processor.getCallbackLock());
        for (int idx : toDelete)
            if (idx >= 0 && idx < (int)processor.nodes.size())
                processor.nodes.erase(processor.nodes.begin() + idx);
    }
    multiSelection.clear();
    selectedIndex = -1;
    repaint();
    return true;
}

void NodeTimeline::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    //--- scroll over an echo node adjusts its probability ---
    int hit = echoNodeAt(e.position);
    if (hit >= 0)
    {
        captureSnapshot();
        bool hitInSel = std::find(multiSelection.begin(), multiSelection.end(), hit)
                        != multiSelection.end();
        const juce::ScopedLock sl(processor.getCallbackLock());
        if (hitInSel && multiSelection.size() > 1)
        {
            for (int i : multiSelection)
                if (i >= 0 && i < (int)processor.nodes.size())
                    processor.nodes[i].probability = juce::jlimit(
                        0.0f, 1.0f, processor.nodes[i].probability + wheel.deltaY);
        }
        else
        {
            processor.nodes[hit].probability = juce::jlimit(
                0.0f, 1.0f, processor.nodes[hit].probability + wheel.deltaY);
        }
        probTooltipIdx  = hit;
        showProbTooltip = true;
        startTimer(1500);
        pushUndoIfChanged();
    }
}
