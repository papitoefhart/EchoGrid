//==============================================================================
// render_ui — offscreen UI snapshot for layout verification (no DAW / no display).
// Builds the real editor, lays it out, paints it into an image and writes a PNG.
// Screencapture can't see the standalone window in this headless env, so this is
// the way to eyeball the layout after UI changes.  Not part of the shipping plugin.
//==============================================================================
#include <JuceHeader.h>
#include <iostream>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/NodeTimeline.h"
#include "../Source/NodeInspector.h"

static void writePng(juce::Component& c, const juce::String& path)
{
    juce::Image img(juce::Image::ARGB, c.getWidth(), c.getHeight(), true);
    { juce::Graphics g(img); c.paintEntireComponent(g, false); }
    juce::File out(path);
    out.deleteFile();
    if (auto os = out.createOutputStream())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream(img, *os);
    }
    std::cout << "rendered: " << out.getFullPathName() << std::endl;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    EchoGridProcessor proc;
    proc.prepareToPlay(44100.0, 512);

    //--- full editor (default GAIN layer) ---
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setSize(1040, 620);          // default window size
    writePng(*ed, "/private/tmp/echogrid_real_render.png");

    //--- timeline alone in PITCH mode, with a few pitched taps, to eyeball the
    //    new overlay (axis labels, centre line, dots, semitone readouts) ---
    if (proc.nodes.size() >= 3)
    {
        proc.nodes[0].pitchSemitones =  7.0f;   // up a 5th
        proc.nodes[1].pitchSemitones = -5.0f;   // down a 4th
        proc.nodes[2].pitchSemitones = 12.0f;   // up an octave
    }
    juce::UndoManager um;
    NodeTimeline tl(proc, um);
    tl.setEditMode(NodeTimeline::EditMode::Pitch);
    tl.setSize(760, 400);
    writePng(tl, "/private/tmp/echogrid_pitch_overlay.png");

    //--- inspector alone with a tap selected, to eyeball the new hero / header /
    //    chip / button styling (the offscreen editor has nothing selected) ---
    if (!proc.nodes.empty())
    {
        proc.nodes[0].gain = 0.84f;
        proc.nodes[0].pan  = -0.24f;
        juce::UndoManager um2;
        NodeTimeline tl2(proc, um2);
        tl2.selectForRender(0);
        EchoGridLAF laf;                       // same look the editor applies to its children
        NodeInspector insp(proc, tl2);
        insp.setLookAndFeel(&laf);
        insp.setSize(1008, 92);
        writePng(insp, "/private/tmp/echogrid_inspector.png");
        insp.setLookAndFeel(nullptr);
    }

    return 0;
}
