#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class NodeTimeline : public juce::Component, private juce::Timer
{
public:
    NodeTimeline(EchoGridProcessor& p, juce::UndoManager& um);

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

    int getSelectedIndex() const { return selectedIndex; }
    const std::vector<int>& getMultiSelection() const { return multiSelection; }
    void clearSelection() { multiSelection.clear(); selectedIndex = -1; repaint(); }

    //--- dev/test only: force a selection for the offscreen render harness ---
    void selectForRender(int i) { multiSelection.assign(1, i); selectedIndex = i; repaint(); }

    //--- overlay edit mode ---
    enum class EditMode { None, Pan, Sat, Pitch };
    void setEditMode(EditMode m) { editMode = m; repaint(); }

    //--- step-sequencer playhead: the editor's timer feeds the host transport position
    //    here; we snap it to the current subdivision cell and repaint only when that cell
    //    changes (cheap — no per-frame repaint).  `playing` false hides the playhead. ---
    void setPlayhead(double songBeats, bool playing);

    //--- undo helpers (also called by NodeInspector and PluginEditor) ---
    void captureSnapshot();
    void pushUndoIfChanged();
    juce::UndoManager& getUndoManager() { return undoManager; }

    struct EditorState
    {
        std::vector<EchoNode> nodes;
        DryNode dry;
        float   gridLengthBeats = 4.0f;
        float   snapStepBeats   = 0.25f;
    };

private:
    EchoGridProcessor& processor;
    juce::UndoManager& undoManager;

    EditorState snapshotBefore;

    //--- coordinate conversion ---
    float beatToX(float beat)  const;
    float gainToY(float gain)  const;
    float xToBeat(float x)     const;
    float yToGain(float y)     const;
    float snapBeat(float beat) const;

    int  echoNodeAt(juce::Point<float>) const;
    bool dryNodeAt(juce::Point<float>)  const;

    //--- line draw (Option + drag) ---
    void placeLineBetween(juce::Point<float> from, juce::Point<float> to);
    bool               drawingLine = false;
    juce::Point<float> lastLinePos;

    //--- drag state ---
    int   dragIndex     = -1;
    float dragStartBeat = 0.0f;
    juce::Point<float> dragStartPos;

    //--- selection ---
    int              selectedIndex  = -1;
    std::vector<int> multiSelection;
    bool             shiftSelecting = false;
    juce::Point<float> shiftSelectStart, shiftSelectCurrent;
    std::vector<float> multiGainAtDragStart;

    //--- overlay edit mode ---
    EditMode editMode = EditMode::None;

    //--- current playhead subdivision index within the grid (-1 = transport stopped) ---
    int      playheadStep = -1;

    //--- pan edit state ---
    int   panDragIdx      = -1;
    float panDragStartPan = 0.0f;
    std::vector<float> multiPanAtDragStart;
    bool               panLineDrawing = false;
    juce::Point<float> panLastLinePos;

    float panToY(float pan) const;
    float yToPan(float y)   const;
    int   panNodeAt(juce::Point<float>) const;
    void  placePanLineBetween(juce::Point<float> from, juce::Point<float> to);

    //--- saturation edit state ---
    int   satDragIdx      = -1;
    float satDragStartSat = 0.0f;
    std::vector<float> multiSatAtDragStart;
    bool               satLineDrawing = false;
    juce::Point<float> satLastLinePos;

    float satToY(float sat) const;
    float yToSat(float y)   const;
    int   satNodeAt(juce::Point<float>) const;
    void  placeSatLineBetween(juce::Point<float> from, juce::Point<float> to);

    //--- pitch edit state (per-tap semitones, ±kPitchRange, snapped to semitones) ---
    static constexpr float kPitchRange = 12.0f;
    int   pitchDragIdx        = -1;
    float pitchDragStartPitch = 0.0f;
    std::vector<float> multiPitchAtDragStart;
    bool               pitchLineDrawing = false;
    juce::Point<float> pitchLastLinePos;

    float pitchToY(float semi) const;
    float yToPitch(float y)    const;
    int   pitchNodeAt(juce::Point<float>) const;
    void  placePitchLineBetween(juce::Point<float> from, juce::Point<float> to);

    //--- probability tooltip ---
    int  probTooltipIdx  = -1;
    bool showProbTooltip = false;

    //--- probability scroll: a run of wheel notches is coalesced into a single
    //    undo step, flushed when the gesture ends (timer fires) or another edit
    //    begins.  true while a scroll gesture is in progress. ---
    bool probGestureActive = false;
    void flushProbGesture();

    void timerCallback() override;
    void updateCursor(juce::Point<float> pos, bool altDown);

    static constexpr float kNodeRadius = 8.0f;
    static constexpr float kPadX       = 24.0f;
    static constexpr float kPadY       = 24.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeTimeline)
};
