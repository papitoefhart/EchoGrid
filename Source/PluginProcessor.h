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

    //--- saturation: 0 = clean, 1 = heavy tape drive ---
    float saturation    = 0.0f;

    //--- state ---
    bool  active        = true;
    bool  reverse       = false;

    bool operator==(const EchoNode& o) const
    {
        return std::memcmp(&positionBeats, &o.positionBeats, sizeof(float)) == 0
            && std::memcmp(&gain,          &o.gain,          sizeof(float)) == 0
            && std::memcmp(&pan,           &o.pan,           sizeof(float)) == 0
            && std::memcmp(&probability,   &o.probability,   sizeof(float)) == 0
            && std::memcmp(&saturation,    &o.saturation,    sizeof(float)) == 0
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
    float                 snapStepBeats   = 0.25f;  // musical snap step in beats (0.25 = 1/16 note)

    //--- filter & mix (editor-writable, audio-thread-read) ---
    float lpCutoffHz  = 20000.0f;  // low-pass  cutoff  (Hz)
    float hpCutoffHz  = 20.0f;     // high-pass cutoff  (Hz)
    bool  filterDry   = false;     // also apply filter to dry signal

private:
    //--- per-node DSP state (audio thread only).  Reverse needs no per-node
    //    state any more: it's a stateless function of the transport position. ---
    struct NodeState
    {
        bool  fired        = true;
        float gainEnvelope = 1.0f;
        float timingJitter = 0.0f;
    };

    void rerollNodes();
    void updateFilterCoefficients();

    //--- NodeState pool ---
    static constexpr int     kMaxNodes          = 32;
    std::vector<NodeState>   nodeStates;
    int                      lastNodeCount       = 0;
    juce::Random             rng;
    int                      samplesUntilReroll  = 0;

    //--- circular delay buffer ---
    juce::AudioBuffer<float> delayBuffer;
    int    writePosition     = 0;
    double currentSampleRate = 44100.0;

    //--- free-running song-position counter (beats), used to grid-lock reverse
    //    windows when the host provides no play position. ---
    double fallbackBeats     = 0.0;

    //--- finite tail length (seconds) for the host: longest active echo delay,
    //    refreshed each block.  Written on the audio thread, read by the host. ---
    std::atomic<double> tailSeconds { 2.0 };

    //--- LP/HP filters: [0]=L [1]=R; wet always filtered, dry filtered if filterDry ---
    juce::IIRFilter lpWet[2], hpWet[2], lpDry[2], hpDry[2];
    float lastLpHz = -1.0f, lastHpHz = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EchoGridProcessor)
};
