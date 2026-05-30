#pragma once
#include <JuceHeader.h>

//==============================================================================
struct DryNode
{
    float gain = 1.0f;
    float pan  = 0.0f;
};

//==============================================================================
struct EchoNode
{
    //--- timing & level ---
    float positionBeats = 1.0f;
    float gain          = 0.7f;

    //--- stereo position ---
    float pan           = 0.0f;

    //--- variability ---
    float probability   = 1.0f;   // 0.0–1.0 chance this echo fires each bar

    //--- state ---
    bool  active        = true;
    bool  reverse       = false;

    bool operator==(const EchoNode& o) const
    {
        // Bit-exact comparison — safe here because we only ever store values
        // that were set directly (no arithmetic rounding between store and compare)
        return std::memcmp(&positionBeats, &o.positionBeats, sizeof(float)) == 0
            && std::memcmp(&gain,          &o.gain,          sizeof(float)) == 0
            && std::memcmp(&pan,           &o.pan,           sizeof(float)) == 0
            && std::memcmp(&probability,   &o.probability,   sizeof(float)) == 0
            && active == o.active && reverse == o.reverse;
    }
    bool operator!=(const EchoNode& o) const { return !(*this == o); }
};

//==============================================================================
class EchoGridProcessor : public juce::AudioProcessor
{
public:
    EchoGridProcessor();
    ~EchoGridProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    bool supportsDoublePrecisionProcessing() const override { return false; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //--- accessed by the editor on the message thread ---
    DryNode               dry;
    std::vector<EchoNode> nodes;
    float                 analogAmount    = 0.0f;   // 0 = precise, 1 = analog
    float                 gridLengthBeats = 4.0f;   // total grid length in beats
    int                   subdivisions    = 16;     // snap subdivisions across the grid

private:
    //--- per-node DSP state (audio thread only) ---
    struct NodeState
    {
        bool  fired        = true;
        float gainEnvelope = 1.0f;
        float timingJitter = 0.0f;

        //--- true reverse double-buffer ---
        //    fill one block while playing the previous block in reverse;
        //    swap when the fill block is complete (= one D-sample period)
        std::vector<float> revL[2], revR[2];
        int  revFillPos = 0;    // write cursor in the fill buffer
        int  revPlayBuf = 0;    // index (0/1) of the buffer being played
        bool revReady   = false; // false until the first buffer is fully recorded
    };

    void rerollNodes();
    void initReverseBuffer(NodeState&);

    //--- NodeState pool: pre-allocated to kMaxNodes in prepareToPlay so that
    //    rerollNodes() can safely be called from the audio thread without any
    //    heap allocation.  Only indices 0..nodes.size()-1 are active at runtime.
    static constexpr int     kMaxNodes          = 32;
    std::vector<NodeState>   nodeStates;         // always kMaxNodes entries after prepareToPlay
    int                      lastNodeCount       = 0;  // nodes.size() at last reroll
    juce::Random             rng;
    int                      samplesUntilReroll  = 0;

    //--- circular delay buffer ---
    juce::AudioBuffer<float> delayBuffer;
    int    writePosition     = 0;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EchoGridProcessor)
};
