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
    void setPanEditMode(bool active) { panEditMode = active; repaint(); }

    //--- undo helpers (also called by NodeInspector and PluginEditor) ---
    void captureSnapshot();
    void pushUndoIfChanged();
    juce::UndoManager& getUndoManager() { return undoManager; }

    struct EditorState
    {
        std::vector<EchoNode> nodes;
        DryNode dry;
        float   gridLengthBeats = 4.0f;  // included so grid-length changes are fully undoable
        int     subdivisions    = 16;
    };

private:
    EchoGridProcessor& processor;
    juce::UndoManager& undoManager;

    //--- undo snapshot ---
    EditorState snapshotBefore;

    //--- coordinate conversion ---
    float beatToX(float beat)  const;
    float gainToY(float gain)  const;
    float xToBeat(float x)     const;
    float yToGain(float y)     const;
    float snapBeat(float beat) const;

    //--- returns echo node index under point, or -1 ---
    int echoNodeAt(juce::Point<float>) const;
    bool dryNodeAt(juce::Point<float>) const;

    //--- line draw (Option + drag) ---
    void placeLineBetween(juce::Point<float> from, juce::Point<float> to);
    bool              drawingLine = false;
    juce::Point<float> lastLinePos;

    //--- drag state ---
    //    dragIndex == -2 → dragging the dry node
    //    dragIndex == -1 → no drag
    //    dragIndex >= 0  → dragging echo node at that index
    int   dragIndex     = -1;
    float dragStartBeat = 0.0f;
    juce::Point<float> dragStartPos;   // anchor for beat drag; Y is set but only X is used

    //--- selection: selectedIndex = inspector target; multiSelection = all selected ---
    int              selectedIndex       = -1;
    std::vector<int> multiSelection;
    bool             shiftSelecting      = false;
    juce::Point<float> shiftSelectStart, shiftSelectCurrent;
    std::vector<float> multiGainAtDragStart;

    //--- pan edit mode ---
    bool  panEditMode       = false;
    int   panDragIdx        = -1;
    float panDragStartPan   = 0.0f;
    std::vector<float> multiPanAtDragStart;
    bool               panLineDrawing  = false;
    juce::Point<float> panLastLinePos;

    float panToY(float pan) const;
    float yToPan(float y)   const;
    int   panNodeAt(juce::Point<float>) const;
    void  placePanLineBetween(juce::Point<float> from, juce::Point<float> to);

    //--- probability scroll tooltip ---
    int  probTooltipIdx  = -1;
    bool showProbTooltip = false;

    void timerCallback() override;
    void updateCursor(juce::Point<float> pos, bool altDown);

    static constexpr float kNodeRadius = 8.0f;
    static constexpr float kPadX       = 24.0f; // left/right margin so nodes don't clip edges
    static constexpr float kPadY       = 24.0f; // top/bottom margin

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeTimeline)
};
