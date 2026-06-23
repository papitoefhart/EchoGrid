#include "NodeTimeline.h"
#include "Theme.h"

//==============================================================================
// Helpers
//==============================================================================
static juce::Colour panToColour(float pan)
{
    //--- pan encoded on the palette: left = blue, centre = lilac, right = pink ---
    return pan < 0.0f ? eg::col::lilac.interpolatedWith(eg::col::blue, -pan)
                      : eg::col::lilac.interpolatedWith(eg::col::pink,  pan);
}

static juce::Colour satToColour(float sat)
{
    //--- soft lilac at 0 → deep rose at 1 ---
    return eg::col::lilacSoft.interpolatedWith(juce::Colour(0xffe07aa6), sat);
}

static juce::Colour pitchToColour(float semi)
{
    //--- unison lilac at 0; cool blue for down, warm pink for up ---
    float t = juce::jlimit(0.0f, 1.0f, std::abs(semi) / 12.0f);
    return semi < 0.0f ? eg::col::lilac.interpolatedWith(eg::col::blue, t)
                       : eg::col::lilac.interpolatedWith(eg::col::pink, t);
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
        proc.snapStepBeats   = after.snapStepBeats;
        return true;
    }

    bool undo() override
    {
        const juce::ScopedLock sl(proc.getCallbackLock());
        proc.nodes           = before.nodes;
        proc.dry             = before.dry;
        proc.gridLengthBeats = before.gridLengthBeats;
        proc.snapStepBeats   = before.snapStepBeats;
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
    snapshotBefore.snapStepBeats   = processor.snapStepBeats;
}

void NodeTimeline::pushUndoIfChanged()
{
    EditorState current { processor.nodes, processor.dry,
                          processor.gridLengthBeats, processor.snapStepBeats };

    //--- bit-exact comparisons to detect any change since captureSnapshot() ---
    bool changed = current.nodes != snapshotBefore.nodes
        || std::memcmp(&current.dry,             &snapshotBefore.dry,             sizeof(DryNode)) != 0
        || std::memcmp(&current.gridLengthBeats, &snapshotBefore.gridLengthBeats, sizeof(float))   != 0
        || std::memcmp(&current.snapStepBeats,   &snapshotBefore.snapStepBeats,   sizeof(float))   != 0;

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
    flushProbGesture();   // gesture ended: commit the accumulated change as one undo step
    repaint();
}

//--- commit a pending probability-scroll gesture as a single undo entry ---
void NodeTimeline::flushProbGesture()
{
    if (!probGestureActive) return;
    probGestureActive = false;
    pushUndoIfChanged();
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

float NodeTimeline::satToY(float sat) const
{
    float usable = (float)getHeight() - 2.0f * kPadY;
    return kPadY + (1.0f - juce::jlimit(0.0f, 1.0f, sat)) * usable;
}

float NodeTimeline::yToSat(float y) const
{
    float usable = (float)getHeight() - 2.0f * kPadY;
    return juce::jlimit(0.0f, 1.0f, 1.0f - (y - kPadY) / usable);
}

int NodeTimeline::satNodeAt(juce::Point<float> p) const
{
    const float xThresh = kNodeRadius + 4.0f;
    int   bestIdx  = -1;
    float bestDist = xThresh + 1.0f;
    for (int i = 0; i < (int)processor.nodes.size(); ++i)
    {
        float dx = std::abs(p.x - beatToX(processor.nodes[i].positionBeats));
        if (dx <= xThresh && dx < bestDist) { bestDist = dx; bestIdx = i; }
    }
    return bestIdx;
}

void NodeTimeline::placeSatLineBetween(juce::Point<float> from, juce::Point<float> to)
{
    float xA    = std::min(from.x, to.x);
    float xB    = std::max(from.x, to.x);
    float xSpan = to.x - from.x;
    const juce::ScopedLock sl(processor.getCallbackLock());
    for (auto& n : processor.nodes)
    {
        float nx = beatToX(n.positionBeats);
        if (nx < xA - kNodeRadius || nx > xB + kNodeRadius) continue;
        float sat;
        if (std::abs(xSpan) < 1.0f)
            sat = yToSat((from.y + to.y) * 0.5f);
        else {
            float t = juce::jlimit(0.0f, 1.0f, (nx - from.x) / xSpan);
            sat = yToSat(from.y + t * (to.y - from.y));
        }
        n.saturation = sat;
    }
}

//==============================================================================
// Pitch overlay helpers — ±kPitchRange semitones, top = up; yToPitch snaps to
// whole semitones so drawn taps land on clean intervals.
//==============================================================================
float NodeTimeline::pitchToY(float semi) const
{
    float usable = (float)getHeight() - 2.0f * kPadY;
    float n = juce::jlimit(-kPitchRange, kPitchRange, semi);
    return kPadY + (kPitchRange - n) / (2.0f * kPitchRange) * usable;
}

float NodeTimeline::yToPitch(float y) const
{
    float usable = (float)getHeight() - 2.0f * kPadY;
    float n = kPitchRange - (y - kPadY) / usable * (2.0f * kPitchRange);
    return std::round(juce::jlimit(-kPitchRange, kPitchRange, n));  // snap to semitones
}

int NodeTimeline::pitchNodeAt(juce::Point<float> p) const
{
    const float xThresh = kNodeRadius + 4.0f;
    int   bestIdx  = -1;
    float bestDist = xThresh + 1.0f;
    for (int i = 0; i < (int)processor.nodes.size(); ++i)
    {
        float dx = std::abs(p.x - beatToX(processor.nodes[i].positionBeats));
        if (dx <= xThresh && dx < bestDist) { bestDist = dx; bestIdx = i; }
    }
    return bestIdx;
}

void NodeTimeline::placePitchLineBetween(juce::Point<float> from, juce::Point<float> to)
{
    float xA    = std::min(from.x, to.x);
    float xB    = std::max(from.x, to.x);
    float xSpan = to.x - from.x;
    const juce::ScopedLock sl(processor.getCallbackLock());
    for (auto& n : processor.nodes)
    {
        float nx = beatToX(n.positionBeats);
        if (nx < xA - kNodeRadius || nx > xB + kNodeRadius) continue;
        float semi;
        if (std::abs(xSpan) < 1.0f)
            semi = yToPitch((from.y + to.y) * 0.5f);
        else {
            float t = juce::jlimit(0.0f, 1.0f, (nx - from.x) / xSpan);
            semi = yToPitch(from.y + t * (to.y - from.y));
        }
        n.pitchSemitones = semi;
    }
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
    //--- snap to the musical grid step (absolute beat value, independent of bar length) ---
    float step = processor.snapStepBeats;
    if (step <= 0.0f) return beat;
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

    //--- rounded panel background (corners left unpainted so the editor's
    //    offset shadow and window background show through them) ---
    auto panelR = juce::Rectangle<float>(0.5f, 0.5f, w - 1.0f, h - 1.0f);
    g.setColour(eg::col::surface);
    g.fillRoundedRectangle(panelR, 16.0f);
    g.setColour(eg::col::line);
    g.drawRoundedRectangle(panelR, 16.0f, 1.0f);

    //--- grid lines: step is an absolute musical value (e.g. 0.25 = 1/16 note),
    //    independent of bar length.  Beat lines (integer positions) are drawn brighter. ---
    const float step     = processor.snapStepBeats;
    const int   numSteps = (int)std::round(processor.gridLengthBeats / step);

    for (int i = 0; i <= numSteps; ++i)
    {
        float pos    = i * step;
        float x      = beatToX(pos);
        float nearest = std::round(pos);
        bool  isBeat  = std::abs(pos - nearest) < step * 0.05f;  // within 5% of a beat position
        g.setColour(isBeat ? juce::Colour(0xffe5dcef) : eg::col::line);
        g.drawVerticalLine((int)x, kPadY * 0.5f, h - kPadY * 0.5f);
    }

    //--- labels: DRY = musical beat 1; beat columns start at 2 so numbers
    //    match what the user hears in their DAW.  Right edge is not labelled
    //    (it is the next bar's beat 1, same as DRY). ---
    g.setFont(10.0f);
    g.setColour(eg::col::ink3);
    g.drawText("1", (int)beatToX(0.0f) - 8, 4, 16, 12,
               juce::Justification::centred, false);
    for (int b = 1; b < (int)std::round(processor.gridLengthBeats); ++b)
        g.drawText(juce::String(b + 1), (int)beatToX((float)b) - 8, 4, 16, 12,
                   juce::Justification::centred, false);

    //--- bottom baseline ---
    g.setColour(juce::Colour(0xffd9cfe4));
    g.drawHorizontalLine((int)(h - kPadY * 0.5f), kPadX, w - kPadX);

    //--- helper: draw one node ---
    //    inactive nodes are rendered grey + dimmed so they read as "off"
    //    reversed nodes show a left-pointing arrow overlay
    auto drawNode = [&](float x, float y, float pan, float probability,
                        bool isReversed, bool active, bool selected)
    {
        auto  c     = active ? panToColour(pan) : juce::Colour(0xffc9c2d3);
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
            g.setColour(juce::Colours::white.withAlpha(juce::jmin(1.0f, alpha + 0.15f)));
            g.fillPath(arrow);
        }

        if (selected)
        {
            g.setColour(eg::col::ink.withAlpha(0.85f));
            g.drawEllipse(x - kNodeRadius - 2.0f, y - kNodeRadius - 2.0f,
                          (kNodeRadius + 2.0f) * 2.0f, (kNodeRadius + 2.0f) * 2.0f, 1.5f);
        }
    };

    //--- dry node ---
    {
        float x = beatToX(0.0f);
        float y = gainToY(processor.dry.gain);
        drawNode(x, y, processor.dry.pan, 1.0f, false, true, selectedIndex == -2);

        g.setColour(eg::col::surface);
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
        g.setColour(eg::col::ink3.withAlpha(nodes[i].active ? 1.0f : 0.5f));
        //--- +1 because DRY = beat 1, so positionBeats=1 → musical beat 2 etc. ---
        g.drawText(juce::String(nodes[i].positionBeats + 1.0f, 2),
                   (int)nx - 18, (int)(ny - kNodeRadius - 14), 36, 11,
                   juce::Justification::centred, false);
    }

    //--- overlay: dim gain layer when any edit mode is active ---
    if (editMode != EditMode::None)
    {
        g.setColour(eg::col::surface.withAlpha(0.68f));
        g.fillRect(0.0f, 0.0f, w, h);
    }

    //--- pan overlay ---
    if (editMode == EditMode::Pan)
    {
        float panCy = panToY(0.0f);
        g.setColour(juce::Colour(0xffd9cfe4));
        g.drawHorizontalLine((int)panCy, kPadX, w - kPadX);

        g.setFont(9.0f);
        g.setColour(eg::col::ink3);
        g.drawText("L", 4, (int)panToY(-1.0f) - 6, 14, 12, juce::Justification::centred, false);
        g.drawText("R", 4, (int)panToY( 1.0f) - 6, 14, 12, juce::Justification::centred, false);

        for (int i = 0; i < (int)nodes.size(); ++i)
        {
            float nx    = beatToX(nodes[i].positionBeats);
            float ny    = panToY(nodes[i].pan);
            float alpha = nodes[i].active ? 0.9f : 0.4f;
            auto  c     = panToColour(nodes[i].pan);
            bool  inSel = std::find(multiSelection.begin(), multiSelection.end(), i)
                          != multiSelection.end();
            g.setColour(c.withAlpha(0.35f * alpha));
            g.drawLine(nx, panCy, nx, ny, 2.0f);
            g.setColour(c.withAlpha(alpha));
            g.fillEllipse(nx - kNodeRadius, ny - kNodeRadius, kNodeRadius * 2.0f, kNodeRadius * 2.0f);
            if (inSel) {
                g.setColour(eg::col::ink.withAlpha(0.85f));
                g.drawEllipse(nx - kNodeRadius - 2.0f, ny - kNodeRadius - 2.0f,
                              (kNodeRadius + 2.0f) * 2.0f, (kNodeRadius + 2.0f) * 2.0f, 1.5f);
            }
        }
        {
            float nx = beatToX(0.0f);
            float ny = panToY(processor.dry.pan);
            auto  c  = panToColour(processor.dry.pan);
            g.setColour(c.withAlpha(0.35f));
            g.drawLine(nx, panCy, nx, ny, 2.0f);
            g.setColour(c.withAlpha(0.85f));
            g.fillEllipse(nx - kNodeRadius, ny - kNodeRadius, kNodeRadius * 2.0f, kNodeRadius * 2.0f);
            g.setColour(eg::col::surface);
            g.fillRect(nx - 3.5f, ny - 3.5f, 7.0f, 7.0f);
            g.setColour(c.withAlpha(0.85f));
            g.drawRect(nx - 3.5f, ny - 3.5f, 7.0f, 7.0f, 1.5f);
        }
        g.setFont(9.0f);
        g.setColour(eg::col::blue.withAlpha(0.95f));
        g.drawText("PAN", (int)(w - 46.0f), 6, 40, 10, juce::Justification::centredRight, false);
    }

    //--- saturation overlay ---
    if (editMode == EditMode::Sat)
    {
        float satBase = satToY(0.0f);
        g.setColour(juce::Colour(0xffd9cfe4));
        g.drawHorizontalLine((int)satBase, kPadX, w - kPadX);

        g.setFont(9.0f);
        g.setColour(eg::col::ink3);
        g.drawText("0",   4, (int)satToY(0.0f) - 6, 18, 12, juce::Justification::centred, false);
        g.drawText("MAX", 4, (int)satToY(1.0f) - 6, 26, 12, juce::Justification::centred, false);

        for (int i = 0; i < (int)nodes.size(); ++i)
        {
            float nx    = beatToX(nodes[i].positionBeats);
            float ny    = satToY(nodes[i].saturation);
            float alpha = nodes[i].active ? 0.9f : 0.4f;
            auto  c     = satToColour(nodes[i].saturation);
            bool  inSel = std::find(multiSelection.begin(), multiSelection.end(), i)
                          != multiSelection.end();
            g.setColour(eg::col::pink.withAlpha(0.5f * alpha));
            g.drawLine(nx, satBase, nx, ny, 2.0f);
            g.setColour(c.withAlpha(alpha));
            g.fillEllipse(nx - kNodeRadius, ny - kNodeRadius, kNodeRadius * 2.0f, kNodeRadius * 2.0f);
            if (inSel) {
                g.setColour(eg::col::ink.withAlpha(0.85f));
                g.drawEllipse(nx - kNodeRadius - 2.0f, ny - kNodeRadius - 2.0f,
                              (kNodeRadius + 2.0f) * 2.0f, (kNodeRadius + 2.0f) * 2.0f, 1.5f);
            }
        }
        g.setFont(9.0f);
        g.setColour(juce::Colour(0xffe07aa6).withAlpha(0.95f));
        g.drawText("SAT", (int)(w - 46.0f), 6, 40, 10, juce::Justification::centredRight, false);
    }

    //--- pitch overlay ---
    if (editMode == EditMode::Pitch)
    {
        float pitchCy = pitchToY(0.0f);
        g.setColour(juce::Colour(0xffd9cfe4));
        g.drawHorizontalLine((int)pitchCy, kPadX, w - kPadX);

        g.setFont(9.0f);
        g.setColour(eg::col::ink3);
        g.drawText("+12", 2, (int)pitchToY( 12.0f) - 6, 22, 12, juce::Justification::centred, false);
        g.drawText("0",   2, (int)pitchToY(  0.0f) - 6, 22, 12, juce::Justification::centred, false);
        g.drawText("-12", 2, (int)pitchToY(-12.0f) - 6, 22, 12, juce::Justification::centred, false);

        for (int i = 0; i < (int)nodes.size(); ++i)
        {
            float nx    = beatToX(nodes[i].positionBeats);
            float ny    = pitchToY(nodes[i].pitchSemitones);
            float alpha = nodes[i].active ? 0.9f : 0.4f;
            auto  c     = pitchToColour(nodes[i].pitchSemitones);
            bool  inSel = std::find(multiSelection.begin(), multiSelection.end(), i)
                          != multiSelection.end();
            g.setColour(c.withAlpha(0.4f * alpha));
            g.drawLine(nx, pitchCy, nx, ny, 2.0f);
            g.setColour(c.withAlpha(alpha));
            g.fillEllipse(nx - kNodeRadius, ny - kNodeRadius, kNodeRadius * 2.0f, kNodeRadius * 2.0f);
            if (inSel) {
                g.setColour(eg::col::ink.withAlpha(0.85f));
                g.drawEllipse(nx - kNodeRadius - 2.0f, ny - kNodeRadius - 2.0f,
                              (kNodeRadius + 2.0f) * 2.0f, (kNodeRadius + 2.0f) * 2.0f, 1.5f);
            }
            //--- semitone readout above the dot (skip 0 = unison) ---
            int st = (int)std::round(nodes[i].pitchSemitones);
            if (st != 0) {
                g.setColour(eg::col::ink2.withAlpha(alpha));
                g.setFont(9.0f);
                juce::String lbl = (st > 0 ? "+" : "") + juce::String(st);
                g.drawText(lbl, (int)nx - 16, (int)(ny - kNodeRadius - 13), 32, 11,
                           juce::Justification::centred, false);
            }
        }
        g.setFont(9.0f);
        g.setColour(eg::col::lilacDeep.withAlpha(0.95f));
        g.drawText("PITCH", (int)(w - 50.0f), 6, 44, 10, juce::Justification::centredRight, false);
    }

    //--- rubber-band rectangle during Shift+drag ---
    if (shiftSelecting)
    {
        juce::Rectangle<float> selRect(
            std::min(shiftSelectStart.x, shiftSelectCurrent.x),
            std::min(shiftSelectStart.y, shiftSelectCurrent.y),
            std::abs(shiftSelectCurrent.x - shiftSelectStart.x),
            std::abs(shiftSelectCurrent.y - shiftSelectStart.y));
        g.setColour(eg::col::lilac.withAlpha(0.18f));
        g.fillRect(selRect);
        g.setColour(eg::col::lilacDeep.withAlpha(0.8f));
        g.drawRect(selRect, 1.5f);
    }

    //--- probability scroll tooltip ---
    if (showProbTooltip && probTooltipIdx >= 0 && probTooltipIdx < (int)nodes.size())
    {
        float nx  = beatToX(nodes[probTooltipIdx].positionBeats);
        float ny  = gainToY(nodes[probTooltipIdx].gain);
        auto  str = juce::String((int)std::round(nodes[probTooltipIdx].probability * 100)) + "%";

        juce::Rectangle<float> pill(nx - 15.0f, ny - kNodeRadius - 27.0f, 32.0f, 16.0f);
        g.setColour(eg::col::ink);
        g.fillRoundedRectangle(pill, 5.0f);
        g.setColour(juce::Colours::white);
        g.setFont(9.0f);
        g.drawText(str, pill.toNearestInt(), juce::Justification::centred, false);
    }
}

//==============================================================================
// Line draw
//==============================================================================
void NodeTimeline::placeLineBetween(juce::Point<float> from, juce::Point<float> to)
{
    const float minBeat = processor.snapStepBeats;
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
    flushProbGesture();   // commit any in-flight scroll gesture before a new edit
    captureSnapshot();

    //--- overlay edit modes (pan / sat): Opt = line draw, Shift = select, normal = drag ---
    auto handleOverlayDown = [&](auto nodeAtFn, auto yToValFn,
                                 auto getVal, auto setVal,
                                 int& dragIdx, float& dragStart,
                                 std::vector<float>& multiStart,
                                 bool& lineDrawing, juce::Point<float>& lastLine,
                                 auto lineDrawFn) -> bool
    {
        if (e.mods.isRightButtonDown()) return false;
        int hit = nodeAtFn(e.position);

        if (e.mods.isAltDown()) {
            lineDrawing = true;  lastLine = e.position;
            lineDrawFn(e.position, e.position);
            repaint();  return true;
        }
        if (e.mods.isShiftDown()) {
            if (hit >= 0) {
                auto it = std::find(multiSelection.begin(), multiSelection.end(), hit);
                if (it != multiSelection.end()) { multiSelection.erase(it); selectedIndex = multiSelection.empty() ? -1 : multiSelection.back(); }
                else { multiSelection.push_back(hit); selectedIndex = hit; }
            } else { shiftSelecting = true; shiftSelectStart = shiftSelectCurrent = e.position; multiSelection.clear(); selectedIndex = -1; }
            repaint();  return true;
        }
        if (hit >= 0) {
            bool inSel = std::find(multiSelection.begin(), multiSelection.end(), hit) != multiSelection.end();
            if (!inSel) { multiSelection = { hit }; selectedIndex = hit; }
            float clickVal = yToValFn(e.position.y);
            { const juce::ScopedLock sl(processor.getCallbackLock()); setVal(hit, clickVal); }
            dragIdx   = hit;  dragStart = clickVal;
            multiStart.clear();
            for (int i : multiSelection)
                if (i >= 0 && i < (int)processor.nodes.size())
                    multiStart.push_back(getVal(i));
        } else { multiSelection.clear(); selectedIndex = -1; }
        repaint();  return true;
    };

    if (editMode == EditMode::Pan) {
        handleOverlayDown(
            [this](auto p){ return panNodeAt(p); },
            [this](float y){ return yToPan(y); },
            [this](int i){ return processor.nodes[i].pan; },
            [this](int i, float v){ processor.nodes[i].pan = v; },
            panDragIdx, panDragStartPan, multiPanAtDragStart,
            panLineDrawing, panLastLinePos,
            [this](auto a, auto b){ placePanLineBetween(a, b); });
        return;
    }
    if (editMode == EditMode::Sat) {
        handleOverlayDown(
            [this](auto p){ return satNodeAt(p); },
            [this](float y){ return yToSat(y); },
            [this](int i){ return processor.nodes[i].saturation; },
            [this](int i, float v){ processor.nodes[i].saturation = v; },
            satDragIdx, satDragStartSat, multiSatAtDragStart,
            satLineDrawing, satLastLinePos,
            [this](auto a, auto b){ placeSatLineBetween(a, b); });
        return;
    }
    if (editMode == EditMode::Pitch) {
        handleOverlayDown(
            [this](auto p){ return pitchNodeAt(p); },
            [this](float y){ return yToPitch(y); },
            [this](int i){ return processor.nodes[i].pitchSemitones; },
            [this](int i, float v){ processor.nodes[i].pitchSemitones = v; },
            pitchDragIdx, pitchDragStartPitch, multiPitchAtDragStart,
            pitchLineDrawing, pitchLastLinePos,
            [this](auto a, auto b){ placePitchLineBetween(a, b); });
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

        float minBeat = processor.snapStepBeats;
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
    //--- rubber-band: use overlay Y when in an edit mode ---
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
            float ny = (editMode == EditMode::Pan)   ? panToY(ns[i].pan)
                     : (editMode == EditMode::Sat)   ? satToY(ns[i].saturation)
                     : (editMode == EditMode::Pitch) ? pitchToY(ns[i].pitchSemitones)
                     : gainToY(ns[i].gain);
            if (selRect.contains(beatToX(ns[i].positionBeats), ny))
                multiSelection.push_back(i);
        }
        selectedIndex = multiSelection.empty() ? -1 : multiSelection.back();
        repaint();
        return;
    }

    //--- pan line draw ---
    if (editMode == EditMode::Pan && panLineDrawing)
    {
        placePanLineBetween(panLastLinePos, e.position);
        panLastLinePos = e.position;  repaint();  return;
    }
    //--- pan dot drag ---
    if (editMode == EditMode::Pan && panDragIdx >= 0 && panDragIdx < (int)processor.nodes.size())
    {
        float pan   = yToPan(e.position.y);
        bool  inSel = std::find(multiSelection.begin(), multiSelection.end(), panDragIdx)
                      != multiSelection.end();
        const juce::ScopedLock sl(processor.getCallbackLock());
        if (inSel && multiSelection.size() > 1 && !multiPanAtDragStart.empty()) {
            float delta = pan - panDragStartPan;
            for (int k = 0; k < (int)std::min(multiSelection.size(), multiPanAtDragStart.size()); ++k)
                if (multiSelection[k] >= 0 && multiSelection[k] < (int)processor.nodes.size())
                    processor.nodes[multiSelection[k]].pan = juce::jlimit(-1.0f, 1.0f, multiPanAtDragStart[k] + delta);
        } else { processor.nodes[panDragIdx].pan = pan; }
        repaint();  return;
    }
    if (editMode == EditMode::Pan) return;  // no other action in pan mode

    //--- sat line draw ---
    if (editMode == EditMode::Sat && satLineDrawing)
    {
        placeSatLineBetween(satLastLinePos, e.position);
        satLastLinePos = e.position;  repaint();  return;
    }
    //--- sat dot drag ---
    if (editMode == EditMode::Sat && satDragIdx >= 0 && satDragIdx < (int)processor.nodes.size())
    {
        float sat   = yToSat(e.position.y);
        bool  inSel = std::find(multiSelection.begin(), multiSelection.end(), satDragIdx)
                      != multiSelection.end();
        const juce::ScopedLock sl(processor.getCallbackLock());
        if (inSel && multiSelection.size() > 1 && !multiSatAtDragStart.empty()) {
            float delta = sat - satDragStartSat;
            for (int k = 0; k < (int)std::min(multiSelection.size(), multiSatAtDragStart.size()); ++k)
                if (multiSelection[k] >= 0 && multiSelection[k] < (int)processor.nodes.size())
                    processor.nodes[multiSelection[k]].saturation = juce::jlimit(0.0f, 1.0f, multiSatAtDragStart[k] + delta);
        } else { processor.nodes[satDragIdx].saturation = sat; }
        repaint();  return;
    }
    if (editMode == EditMode::Sat) return;  // no other action in sat mode

    //--- pitch line draw ---
    if (editMode == EditMode::Pitch && pitchLineDrawing)
    {
        placePitchLineBetween(pitchLastLinePos, e.position);
        pitchLastLinePos = e.position;  repaint();  return;
    }
    //--- pitch dot drag ---
    if (editMode == EditMode::Pitch && pitchDragIdx >= 0 && pitchDragIdx < (int)processor.nodes.size())
    {
        float semi  = yToPitch(e.position.y);
        bool  inSel = std::find(multiSelection.begin(), multiSelection.end(), pitchDragIdx)
                      != multiSelection.end();
        const juce::ScopedLock sl(processor.getCallbackLock());
        if (inSel && multiSelection.size() > 1 && !multiPitchAtDragStart.empty()) {
            float delta = semi - pitchDragStartPitch;
            for (int k = 0; k < (int)std::min(multiSelection.size(), multiPitchAtDragStart.size()); ++k)
                if (multiSelection[k] >= 0 && multiSelection[k] < (int)processor.nodes.size())
                    processor.nodes[multiSelection[k]].pitchSemitones =
                        juce::jlimit(-kPitchRange, kPitchRange, std::round(multiPitchAtDragStart[k] + delta));
        } else { processor.nodes[pitchDragIdx].pitchSemitones = semi; }
        repaint();  return;
    }
    if (editMode == EditMode::Pitch) return;  // no other action in pitch mode

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
    float minBeat = processor.snapStepBeats;
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
    drawingLine    = false;
    shiftSelecting = false;
    dragIndex      = -1;
    panDragIdx     = -1;  panLineDrawing   = false;
    satDragIdx     = -1;  satLineDrawing   = false;
    pitchDragIdx   = -1;  pitchLineDrawing = false;
    multiGainAtDragStart.clear();
    multiPanAtDragStart.clear();
    multiSatAtDragStart.clear();
    multiPitchAtDragStart.clear();
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
    flushProbGesture();   // commit any in-flight scroll gesture before undo/redo or delete

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
        //--- start a new gesture only on the first notch; subsequent notches
        //    accumulate against the same snapshot so the whole scroll is one undo ---
        if (!probGestureActive) { captureSnapshot(); probGestureActive = true; }

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
        startTimer(1500);   // (re)arm; gesture commits when the timer finally fires
        repaint();
    }
}
