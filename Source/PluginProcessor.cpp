#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
EchoGridProcessor::EchoGridProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    nodes.push_back({ 1.0f, 0.70f,  0.0f, 1.0f, true });
    nodes.push_back({ 2.0f, 0.50f,  0.5f, 1.0f, true });
    nodes.push_back({ 3.0f, 0.30f, -0.5f, 1.0f, true });
}

EchoGridProcessor::~EchoGridProcessor() {}

//==============================================================================
// Boilerplate
//==============================================================================
const juce::String EchoGridProcessor::getName() const        { return JucePlugin_Name; }
bool EchoGridProcessor::acceptsMidi() const                  { return false; }
bool EchoGridProcessor::producesMidi() const                 { return false; }
bool EchoGridProcessor::isMidiEffect() const                 { return false; }
double EchoGridProcessor::getTailLengthSeconds() const       { return std::numeric_limits<double>::infinity(); }
int EchoGridProcessor::getNumPrograms()                      { return 1; }
int EchoGridProcessor::getCurrentProgram()                   { return 0; }
void EchoGridProcessor::setCurrentProgram(int)               {}
const juce::String EchoGridProcessor::getProgramName(int)    { return {}; }
void EchoGridProcessor::changeProgramName(int, const juce::String&) {}
void EchoGridProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement root("EchoGrid");

    //--- global ---
    root.setAttribute("dryGain",          (double)dry.gain);
    root.setAttribute("dryPan",           (double)dry.pan);
    root.setAttribute("analogAmount",     (double)analogAmount);
    root.setAttribute("gridLengthBeats",  (double)gridLengthBeats);
    root.setAttribute("subdivisions",     subdivisions);

    //--- echo nodes ---
    for (const auto& n : nodes)
    {
        auto* e = root.createNewChildElement("Node");
        e->setAttribute("positionBeats", (double)n.positionBeats);
        e->setAttribute("gain",          (double)n.gain);
        e->setAttribute("pan",           (double)n.pan);
        e->setAttribute("probability",   (double)n.probability);
        e->setAttribute("active",        n.active);
        e->setAttribute("reverse",       n.reverse);
    }

    copyXmlToBinary(root, destData);
}

void EchoGridProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName("EchoGrid")) return;

    dry.gain         = (float)xml->getDoubleAttribute("dryGain",         1.0);
    dry.pan          = (float)xml->getDoubleAttribute("dryPan",          0.0);
    analogAmount     = (float)xml->getDoubleAttribute("analogAmount",    0.0);
    gridLengthBeats  = (float)xml->getDoubleAttribute("gridLengthBeats", 4.0);
    subdivisions     = xml->getIntAttribute("subdivisions", 16);

    const juce::ScopedLock sl(getCallbackLock());
    nodes.clear();

    for (auto* e : xml->getChildIterator())
    {
        if (!e->hasTagName("Node")) continue;
        EchoNode n;
        n.positionBeats = (float)e->getDoubleAttribute("positionBeats", 1.0);
        n.gain          = (float)e->getDoubleAttribute("gain",          0.7);
        n.pan           = (float)e->getDoubleAttribute("pan",           0.0);
        n.probability   = (float)e->getDoubleAttribute("probability",   1.0);
        n.active        = e->getBoolAttribute("active",  true);
        n.reverse       = e->getBoolAttribute("reverse", false);
        nodes.push_back(n);
    }
}

//==============================================================================
// Lifecycle
//==============================================================================
void EchoGridProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;

    //--- circular delay buffer: max 32 s covers any musical tempo ---
    int maxDelaySamples = static_cast<int>(sampleRate * 32.0);
    delayBuffer.setSize(2, maxDelaySamples);
    delayBuffer.clear();
    writePosition      = 0;
    samplesUntilReroll = 0;
    lastNodeCount      = 0;

    //--- pre-allocate the full NodeState pool so that rerollNodes() never
    //    calls any allocator on the audio thread (Fix: real-time safety) ---
    nodeStates.resize(kMaxNodes);
    for (auto& st : nodeStates)
        initReverseBuffer(st);

    //--- seed fired values for the currently active nodes ---
    rerollNodes();
}

void EchoGridProcessor::releaseResources()
{
    delayBuffer.clear();
    writePosition = 0;
}

bool EchoGridProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::stereo();
}

//==============================================================================
// Variability
//==============================================================================
void EchoGridProcessor::initReverseBuffer(NodeState& st)
{
    if (currentSampleRate <= 0.0) return;
    const int maxD = static_cast<int>(currentSampleRate * 4.0); // 4 sec max
    for (int b = 0; b < 2; ++b)
    {
        st.revL[b].assign(maxD, 0.0f);
        st.revR[b].assign(maxD, 0.0f);
    }
    st.revFillPos = 0;
    st.revPlayBuf = 0;
    st.revReady   = false;
}

void EchoGridProcessor::rerollNodes()
{
    //--- nodeStates is pre-allocated to kMaxNodes; we NEVER resize here so
    //    this function is safe to call from the audio thread.
    //    We only update the fired flags for the currently active node range. ---
    const int activeCount = juce::jmin((int)nodes.size(), kMaxNodes);
    for (int i = 0; i < activeCount; ++i)
        nodeStates[i].fired = (rng.nextFloat() < nodes[i].probability);

    lastNodeCount = (int)nodes.size();
}

//==============================================================================
// Audio engine
//==============================================================================
void EchoGridProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    //--- get host BPM (fall back to 120 if unavailable) ---
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;

    const int    numSamples  = buffer.getNumSamples();
    const int    bufSize     = delayBuffer.getNumSamples();
    const int    numChannels = juce::jmin(buffer.getNumChannels(), delayBuffer.getNumChannels());

    if (bufSize == 0 || numChannels == 0) return;

    const double samplesPerBeat = (60.0 / bpm) * currentSampleRate;

    //--- reroll fired flags when the node count changes (no allocation —
    //    nodeStates pool is always kMaxNodes entries after prepareToPlay) ---
    if (lastNodeCount != (int)nodes.size())
        rerollNodes();

    //--- probability reroll: once per bar (4 beats) ---
    samplesUntilReroll -= numSamples;
    if (samplesUntilReroll <= 0)
    {
        rerollNodes();
        samplesUntilReroll = static_cast<int>(samplesPerBeat * gridLengthBeats);
    }

    //--- update per-node analog timing jitter (slow random walk) ---
    //    max jitter = analogAmount * 5% of delay time
    //    clamped to active node range (never exceeds kMaxNodes) ---
    const float jitterRate   = 0.002f * analogAmount;
    const int   activeCount  = juce::jmin((int)nodes.size(), kMaxNodes);
    for (int i = 0; i < activeCount; ++i)
    {
        float drift = (rng.nextFloat() * 2.0f - 1.0f) * jitterRate
                      * (float)(nodes[i].positionBeats * samplesPerBeat);
        nodeStates[i].timingJitter += drift;
        nodeStates[i].timingJitter *= 0.998f;
    }

    //--- smoothing coefficient: ~8ms click-free gain transitions ---
    const float smoothCoeff = 1.0f - std::exp(-1.0f / (0.008f * (float)currentSampleRate));

    //--- sample-by-sample loop ---
    for (int s = 0; s < numSamples; ++s)
    {
        const float rawL = buffer.getSample(0, s);
        const float rawR = (numChannels > 1) ? buffer.getSample(1, s) : rawL;

        //--- dry signal ---
        {
            float angle = (dry.pan * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
            buffer.setSample(0, s, rawL * dry.gain * std::cos(angle));
            if (numChannels > 1)
                buffer.setSample(1, s, rawR * dry.gain * std::sin(angle));
        }

        //--- echo nodes (guarded to pool size so nodeStates[i] is always valid) ---
        for (int i = 0; i < (int)nodes.size() && i < kMaxNodes; ++i)
        {
            const auto& node  = nodes[i];
            auto&       state = nodeStates[i];

            if (!node.active) { state.gainEnvelope = 0.0f; continue; }

            //--- smooth gain ---
            float targetGain = state.fired ? node.gain : 0.0f;
            state.gainEnvelope += smoothCoeff * (targetGain - state.gainEnvelope);

            if (state.gainEnvelope < 0.0001f) continue;

            int baseOffset = juce::jlimit(1, bufSize - 1,
                static_cast<int>(node.positionBeats * samplesPerBeat));

            float angle = (node.pan * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;

            if (node.reverse)
            {
                //--- true reverse: double-buffer ---
                //    while filling block N, play block N-1 in reverse;
                //    swap buffers when the fill block is full (every D samples)
                int D = state.revL[0].empty()
                            ? 0 : juce::jmin(baseOffset, (int)state.revL[0].size());
                if (D < 1) continue;

                //--- reset fill cursor if D shrank (e.g. node moved or BPM change) ---
                if (state.revFillPos >= D) { state.revFillPos = 0; state.revReady = false; }

                const int fillBuf = 1 - state.revPlayBuf;

                //--- record raw input into fill buffer ---
                state.revL[fillBuf][state.revFillPos] = rawL;
                state.revR[fillBuf][state.revFillPos] = rawR;

                //--- output reversed play buffer ---
                if (state.revReady)
                {
                    const int playPos = D - 1 - state.revFillPos;
                    buffer.addSample(0, s,
                        state.revL[state.revPlayBuf][playPos]
                        * state.gainEnvelope * std::cos(angle));
                    if (numChannels > 1)
                        buffer.addSample(1, s,
                            state.revR[state.revPlayBuf][playPos]
                            * state.gainEnvelope * std::sin(angle));
                }

                //--- advance fill cursor; swap when block is complete ---
                if (++state.revFillPos >= D)
                {
                    state.revFillPos = 0;
                    state.revPlayBuf = fillBuf;
                    state.revReady   = true;
                }
            }
            else
            {
                //--- forward echo: read from circular delay buffer ---
                int readOffset = juce::jlimit(1, bufSize - 1,
                    baseOffset + static_cast<int>(state.timingJitter));
                int readPos = (writePosition - readOffset + bufSize) % bufSize;

                buffer.addSample(0, s,
                    delayBuffer.getSample(0, readPos) * state.gainEnvelope * std::cos(angle));
                if (numChannels > 1)
                    buffer.addSample(1, s,
                        delayBuffer.getSample(1, readPos) * state.gainEnvelope * std::sin(angle));
            }
        }

        //--- write only dry input to delay buffer ---
        delayBuffer.setSample(0, writePosition, rawL);
        if (numChannels > 1)
            delayBuffer.setSample(1, writePosition, rawR);

        writePosition = (writePosition + 1) % bufSize;
    }

    //--- report latency for active reverse nodes so the DAW can compensate ---
    int maxRevSamples = 0;
    for (int i = 0; i < (int)nodes.size(); ++i)
    {
        if (nodes[i].active && nodes[i].reverse)
        {
            int D = static_cast<int>(nodes[i].positionBeats * samplesPerBeat);
            maxRevSamples = juce::jmax(maxRevSamples, D);
        }
    }
    if (maxRevSamples != getLatencySamples())
        setLatencySamples(maxRevSamples);
}

//==============================================================================
// Editor
//==============================================================================
bool EchoGridProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* EchoGridProcessor::createEditor()
{
    return new EchoGridEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EchoGridProcessor();
}
