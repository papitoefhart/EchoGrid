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
double EchoGridProcessor::getTailLengthSeconds() const       { return tailSeconds.load(); }
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
    root.setAttribute("snapStepBeats",    (double)snapStepBeats);
    root.setAttribute("lpCutoffHz",       (double)lpCutoffHz);
    root.setAttribute("hpCutoffHz",       (double)hpCutoffHz);
    root.setAttribute("filterDry",        filterDry);

    //--- echo nodes ---
    for (const auto& n : nodes)
    {
        auto* e = root.createNewChildElement("Node");
        e->setAttribute("positionBeats", (double)n.positionBeats);
        e->setAttribute("gain",          (double)n.gain);
        e->setAttribute("pan",           (double)n.pan);
        e->setAttribute("probability",   (double)n.probability);
        e->setAttribute("saturation",    (double)n.saturation);
        e->setAttribute("active",        n.active);
        e->setAttribute("reverse",       n.reverse);
        e->setAttribute("reverseLength", (double)n.reverseLength);
        e->setAttribute("reverseLock",   n.reverseLock);
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
    lpCutoffHz       = (float)xml->getDoubleAttribute("lpCutoffHz",      20000.0);
    hpCutoffHz       = (float)xml->getDoubleAttribute("hpCutoffHz",      20.0);
    filterDry        = xml->getBoolAttribute("filterDry", false);
    //--- load snap step; support old preset files that stored an int subdivisions count ---
    if (xml->hasAttribute("snapStepBeats"))
        snapStepBeats = (float)xml->getDoubleAttribute("snapStepBeats", 0.25);
    else
    {
        //--- legacy conversion: old subdivisions was a count across the bar;
        //    assume 4-beat bar so step = 4 / subdivisions beats ---
        int oldDiv = xml->getIntAttribute("subdivisions", 16);
        snapStepBeats = 4.0f / juce::jmax(1, oldDiv);
    }

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
        n.saturation    = (float)e->getDoubleAttribute("saturation",    0.0);
        n.active        = e->getBoolAttribute("active",  true);
        n.reverse       = e->getBoolAttribute("reverse", false);
        n.reverseLength = (float)e->getDoubleAttribute("reverseLength", 1.0);
        n.reverseLock   = e->getBoolAttribute("reverseLock", true);
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
    setLatencySamples(0);

    //--- pre-allocate the full NodeState pool and reset all state ---
    nodeStates.resize(kMaxNodes);
    for (auto& st : nodeStates)
    {
        st.gainEnvelope = 1.0f;
        st.timingJitter = 0.0f;
        st.fired        = true;
    }
    fallbackBeats = 0.0;

    //--- seed fired values for the currently active nodes ---
    rerollNodes();

    //--- reset and initialise LP/HP filters ---
    for (int ch = 0; ch < 2; ++ch)
    {
        lpWet[ch].reset(); hpWet[ch].reset();
        lpDry[ch].reset(); hpDry[ch].reset();
    }
    lastLpHz = -1.0f; lastHpHz = -1.0f;  // force coefficient update on first block
    updateFilterCoefficients();
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

void EchoGridProcessor::rerollNodes()
{
    //--- nodeStates pool is pre-allocated to kMaxNodes in prepareToPlay; we
    //    never resize here so this is safe to call from the audio thread. ---
    const int activeCount = juce::jmin((int)nodes.size(), kMaxNodes);

    if (lastNodeCount != activeCount)
    {
        //--- Node list changed.  Preserve existing lower-index slot state so
        //    active reverses aren't stomped when the user edits the node list.
        //    Only initialise newly allocated slots or clear now-unused slots.
        if (activeCount > lastNodeCount)
        {
            // initialise only the new slots between lastNodeCount..activeCount-1
            for (int i = lastNodeCount; i < activeCount; ++i)
            {
                auto& st = nodeStates[i];
                st.gainEnvelope = 0.0f; st.timingJitter = 0.0f; st.fired = true;
            }
        }
        else
        {
            for (int i = activeCount; i < lastNodeCount && i < kMaxNodes; ++i)
            {
                auto& st = nodeStates[i];
                st.gainEnvelope = 0.0f; st.timingJitter = 0.0f; st.fired = false;
            }
        }
    }

    for (int i = 0; i < activeCount; ++i)
        nodeStates[i].fired = (rng.nextFloat() < nodes[i].probability);

    lastNodeCount = activeCount;
}

//==============================================================================
// Filter helper
//==============================================================================
void EchoGridProcessor::updateFilterCoefficients()
{
    const float nyquist = (float)currentSampleRate * 0.49f;
    const float lp = juce::jlimit(200.0f,  nyquist, lpCutoffHz);
    const float hp = juce::jlimit(20.0f,   lp * 0.9f, hpCutoffHz);

    auto lpC = juce::IIRCoefficients::makeLowPass (currentSampleRate, (double)lp);
    auto hpC = juce::IIRCoefficients::makeHighPass(currentSampleRate, (double)hp);

    for (int ch = 0; ch < 2; ++ch)
    {
        lpWet[ch].setCoefficients(lpC); hpWet[ch].setCoefficients(hpC);
        lpDry[ch].setCoefficients(lpC); hpDry[ch].setCoefficients(hpC);
    }
    lastLpHz = lpCutoffHz;
    lastHpHz = hpCutoffHz;
}

//==============================================================================
// Audio engine
//==============================================================================
void EchoGridProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    //--- get host BPM + song position (fall back to 120 BPM / free-run) ---
    double bpm          = 120.0;
    double hostPpq      = 0.0;
    bool   havePpq      = false;
    bool   isPlaying    = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b  = pos->getBpm())             bpm     = *b;
            if (auto q  = pos->getPpqPosition())   { hostPpq = *q; havePpq = true; }
            isPlaying = pos->getIsPlaying();
        }

    const int    numSamples  = buffer.getNumSamples();
    const int    bufSize     = delayBuffer.getNumSamples();
    const int    numChannels = juce::jmin(buffer.getNumChannels(), delayBuffer.getNumChannels());

    if (bufSize == 0 || numChannels == 0) return;

    const double samplesPerBeat = (60.0 / bpm) * currentSampleRate;

    //--- song position in beats at the start of this block, used to phase-lock the
    //    reverse chunks.  While the transport is rolling we follow the host
    //    (deterministic, grid-aligned); when it's stopped (e.g. playing live with no
    //    transport) the host freezes its ppq, so we fall back to a free-running
    //    counter that keeps advancing every block. ---
    const bool   useHostPos = havePpq && isPlaying;
    const double songBeats0 = useHostPos ? hostPpq : fallbackBeats;

    //--- start of the current PATTERN (every gridLengthBeats), the anchor the
    //    reverse chunks re-sync to.  Anchoring to the pattern (not song-start) keeps
    //    the reverse identical every bar for ANY tap position — without it, chunks
    //    whose length doesn't divide the pattern drift bar-to-bar and land in the
    //    wrong place (or nowhere).  The pattern downbeat is always a chunk edge. ---
    const double patBeats   = juce::jmax(0.25, (double)gridLengthBeats);
    const double barStart0  = std::floor(songBeats0 / patBeats) * patBeats;

    //--- minimum reverse window length (~60 ms): the floor that stops a reverse
    //    tap from collapsing into buzz as it approaches the dry.  The window
    //    follows the tap delay but never drops below this. ---
    const int minRevWindow = juce::jmax(1, (int)(0.060 * currentSampleRate));

    //--- reverse splice crossfade length (~4 ms): short seam fade that de-clicks
    //    window boundaries without a second stream (which would ghost). ---
    const int spliceXfade = juce::jmax(1, (int)(0.004 * currentSampleRate));

    //--- cache a finite tail length for the host: the longest active echo delay.
    //    Reverse nodes need ~2×D (record D, then play it back), and there's no
    //    feedback, so the tail never exceeds this.  Clamped to the buffer length. ---
    {
        double maxTailSamples = 0.0;
        const int tailCount = juce::jmin((int)nodes.size(), kMaxNodes);
        for (int i = 0; i < tailCount; ++i)
        {
            if (!nodes[i].active) continue;
            double d = nodes[i].positionBeats * samplesPerBeat;
            if (nodes[i].reverse) d *= 2.0;
            maxTailSamples = juce::jmax(maxTailSamples, d);
        }
        tailSeconds.store(juce::jmin((double)bufSize, maxTailSamples) / currentSampleRate);
    }

    //--- Zero-latency by design: this is the real-time delay mode, so dry passes
    //    through instantly and you can play/record live through the plugin.  Echo
    //    offsets (including reverse) are the effect itself and are NOT latency-
    //    compensated — reporting latency here is the tape-style behaviour we chose
    //    against.  Latency is set once to 0 in prepareToPlay and never touched on
    //    the audio thread. ---

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

    //--- analog timing jitter: disabled until the analog knob is re-enabled.
    //    The decay keeps timingJitter settled at 0 so forward echo offsets stay clean.
    const int activeCount = juce::jmin((int)nodes.size(), kMaxNodes);
    for (int i = 0; i < activeCount; ++i)
        nodeStates[i].timingJitter *= 0.998f;

    //--- smoothing coefficient: ~8ms click-free gain transitions ---
    const float smoothCoeff = 1.0f - std::exp(-1.0f / (0.008f * (float)currentSampleRate));

    //--- update LP/HP filter coefficients if the user changed them ---
    if (std::abs(lpCutoffHz - lastLpHz) > 0.5f || std::abs(hpCutoffHz - lastHpHz) > 0.5f)
        updateFilterCoefficients();

    //--- sample-by-sample audio processing loop ---
    //    dry signal + echo nodes + filters + write to circular delay buffer.
    for (int s = 0; s < numSamples; ++s)
    {
        const float rawL = buffer.getSample(0, s);
        const float rawR = (numChannels > 1) ? buffer.getSample(1, s) : rawL;

        //--- dry signal mix: apply gain and pan to the direct input path ---
        const float dryAngle = (dry.pan * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
        float dryL = rawL * dry.gain * std::cos(dryAngle);
        float dryR = (numChannels > 1) ? rawR * dry.gain * std::sin(dryAngle) : dryL;

        //--- wet signal accumulator (echo nodes add into this) ---
        float wetL = 0.0f, wetR = 0.0f;

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

            float echoL, echoR;

            if (node.reverse)
            {
                //--- Reverse delay that SWELLS INTO THE BEAT: a chunk of audio is
                //    played back reversed (newest sample first → the attack last), so
                //    the loud transient lands at the tap position, the reverse
                //    crescendoing up into it.
                //
                //    REV LEN (node.reverseLength, 0..1) sets the length of each
                //    reversed chunk; the IN-TIME / FREE toggle (node.reverseLock) sets
                //    how that length relates to the beat:
                //
                //    • IN TIME (locked): chunk runs 0..½-tap; whatever the length, the
                //      chunk is read one extra (D − 2·chunk) back so the attack still
                //      lands exactly at the tap delay D.  (A reversed chunk's attack
                //      naturally lands 2 chunk-lengths late, so 2·chunk + (D − 2·chunk)
                //      = D.)  Shorter = a tighter, grainier reverse; full = the smooth
                //      half-tap swell (the v20 default).
                //
                //    • FREE: chunk runs 0..full-tap with no shift, so the attack lands
                //      at 2·chunk — on the beat at ½-tap, drifting later beyond that.
                //
                //    Chunks are phase-locked to the pattern start (barStart0) so the
                //    reverse repeats identically every bar, with a short splice
                //    crossfade at each chunk seam. ---

                const double Wmin = (double)minRevWindow / samplesPerBeat;
                const double knob = juce::jlimit(0.0, 1.0, (double)node.reverseLength);

                //--- chunk length (beats) + the extra read-back that keeps the attack
                //    on the beat in IN-TIME mode (zero in FREE mode) ---
                double Wbeats, shiftBeats;
                if (node.reverseLock)
                {
                    Wbeats     = juce::jmax(Wmin, knob * (double)node.positionBeats * 0.5);
                    shiftBeats = juce::jmax(0.0, (double)node.positionBeats - 2.0 * Wbeats);
                }
                else
                {
                    Wbeats     = juce::jmax(Wmin, knob * (double)node.positionBeats);
                    shiftBeats = 0.0;
                }

                const int G     = juce::jmax(1, (int)std::lround(Wbeats * samplesPerBeat));
                const int shift = juce::jmax(0, (int)std::lround(shiftBeats * samplesPerBeat));
                const int X     = juce::jlimit(1, juce::jmax(1, G / 4), spliceXfade);

                //--- phase within the current chunk, anchored to the pattern start ---
                const double songBeats  = songBeats0 + (double)s / samplesPerBeat;
                const double delRel     = songBeats - barStart0;
                const double phaseBeats = delRel - std::floor(delRel / Wbeats) * Wbeats;
                const int    p          = juce::jlimit(0, G - 1, (int)(phaseBeats * samplesPerBeat));

                //--- read the chunk backwards (newest→oldest), shifted so the attack
                //    lands at the tap delay (IN TIME) or at 2·chunk (FREE) ---
                const int offP = juce::jlimit(1, bufSize - 1, shift + 2 * p + 1);
                const int rP   = (writePosition - offP + bufSize) % bufSize;
                echoL = delayBuffer.getSample(0, rP);
                echoR = (numChannels > 1) ? delayBuffer.getSample(1, rP) : echoL;

                //--- seam crossfade: the previous chunk's reverse tail (2·G older)
                //    continues OUT under the new chunk — click-free, single stream ---
                if (p < X)
                {
                    const int offO = juce::jlimit(1, bufSize - 1, shift + 2 * p + 2 * G + 1);
                    const int rO   = (writePosition - offO + bufSize) % bufSize;
                    const float t  = (float)p / (float)X;                            // 0→1
                    const float wN = std::sin(t * juce::MathConstants<float>::halfPi); // new in
                    const float wO = std::cos(t * juce::MathConstants<float>::halfPi); // old out

                    echoL = wN * echoL + wO * delayBuffer.getSample(0, rO);
                    echoR = (numChannels > 1)
                          ? wN * echoR + wO * delayBuffer.getSample(1, rO)
                          : echoL;
                }
            }
            else
            {
                //--- forward echo playback path ---
                //    normal delay read from the circular buffer, with any jitter
                //    currently disabled.
                const int readOffset = juce::jlimit(1, bufSize - 1,
                    baseOffset + static_cast<int>(state.timingJitter));
                const int readPos = (writePosition - readOffset + bufSize) % bufSize;

                echoL = delayBuffer.getSample(0, readPos);
                echoR = (numChannels > 1) ? delayBuffer.getSample(1, readPos) : echoL;
            }

            //--- tape saturation is disabled for now.
            //    The saturation code is preserved but bypassed so the reverse
            //    playback path remains simpler during debugging.
            // {
            //     const float drive = 1.0f + node.saturation * node.saturation * 10.0f;
            //     const float norm  = 1.0f / std::tanh(drive);
            //     echoL = std::tanh(echoL * drive) * norm;
            //     echoR = std::tanh(echoR * drive) * norm;
            // }

            wetL += echoL * state.gainEnvelope * std::cos(angle);
            if (numChannels > 1)
                wetR += echoR * state.gainEnvelope * std::sin(angle);
        }

        //--- LP/HP filter: wet always, dry only when filterDry is on.
        //    Dry filters run every sample regardless so their state stays warm
        //    (no click when filterDry is toggled). ---
        wetL = lpWet[0].processSingleSampleRaw(hpWet[0].processSingleSampleRaw(wetL));
        if (numChannels > 1)
            wetR = lpWet[1].processSingleSampleRaw(hpWet[1].processSingleSampleRaw(wetR));

        const float filtDryL = lpDry[0].processSingleSampleRaw(hpDry[0].processSingleSampleRaw(dryL));
        const float filtDryR = (numChannels > 1)
            ? lpDry[1].processSingleSampleRaw(hpDry[1].processSingleSampleRaw(dryR)) : filtDryL;

        const float outDryL = filterDry ? filtDryL : dryL;
        const float outDryR = filterDry ? filtDryR : dryR;

        //--- output: dry + wet ---
        buffer.setSample(0, s, outDryL + wetL);
        if (numChannels > 1)
            buffer.setSample(1, s, outDryR + wetR);

        //--- write raw input to delay buffer ---
        delayBuffer.setSample(0, writePosition, rawL);
        if (numChannels > 1)
            delayBuffer.setSample(1, writePosition, rawR);

        writePosition = (writePosition + 1) % bufSize;
    }

    //--- advance the free-running song-position counter so reverse windows stay
    //    continuous across blocks when the host gives no play position. ---
    fallbackBeats = songBeats0 + (double)numSamples / samplesPerBeat;
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
