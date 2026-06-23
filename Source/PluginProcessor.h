#pragma once
#include <JuceHeader.h>

//==============================================================================
// Tape / drive soft-clip — the shared saturation curve.  drive ∈ [0,1]; drive 0
// returns the input untouched.  Asymmetric (bias) so it adds EVEN + odd harmonics
// (warmth, not just fizz); gentle/cassette-ish at low drive, harder/overdriven
// toward the top.  Output normalised to the louder (negative) side so it stays ±1.
//==============================================================================
static inline float tapeShape(float x, float drive) noexcept
{
    if (drive <= 0.0001f) return x;
    const float k   = 1.0f + drive * 5.0f;                 // pre-gain into the shaper
    const float b   = 0.15f * drive;                       // asymmetry → even harmonics
    const float dc  = std::tanh(b);                        // DC the bias introduces
    const float nrm = 1.0f / (std::tanh(k - b) + dc);      // keep peak magnitude ≈ 1
    return (std::tanh(k * x + b) - dc) * nrm;
}

//==============================================================================
// TapeStage — the COMPLETE "through the 424" tape stage, as one reusable unit so
// the global DRIVE and the per-tap SAT slider sound identical for the same drive.
//
// Chain (record→playback order): compression → soft-clip → head bump → HF loss,
// then AUTO GAIN COMPENSATION so more drive = more character, NOT more level.
// One instance per place it's used (global output + each tap), each carrying its
// own filter / follower state.  All tuning constants live in setDrive().
//==============================================================================
struct TapeStage
{
    //--- running state ---
    float           env    = 0.0f;            // stereo-linked compression follower
    float           lp[2]  = { 0.0f, 0.0f };  // drive-linked high-cut (one-pole)
    juce::IIRFilter bump[2];                   // playback-head low bump
    float           inLvl  = 0.0f;            // gain-comp: pre-stage level follower
    float           outLvl = 0.0f;            // gain-comp: post-stage level follower

    //--- per-block snapshot, recomputed only when drive changes ---
    float lastDrive  = -1.0f;
    float lpA        = 1.0f;                   // high-cut coefficient
    float compThresh = 1.0f, compSlope = 0.0f;
    float envAtk = 1.0f, envRel = 1.0f;        // follower attack / release
    float lvlCoeff = 0.0f;                     // gain-comp follower speed

    void reset()
    {
        env = 0.0f; lp[0] = lp[1] = 0.0f;
        bump[0].reset(); bump[1].reset();
        inLvl = outLvl = 0.0f;
        lastDrive = -1.0f;
    }

    //--- recompute the drive-linked constants (call once per block).  CALIBRATION
    //    LIVES HERE — every "how much" number for the tape character is below. ---
    void setDrive(float drive, double sr)
    {
        if (std::abs(drive - lastDrive) <= 0.0001f) return;
        lastDrive = drive;

        //--- HF loss: high-cut falls ~18k → 5k as you push (reads as "tape").
        //    Dialed back from the earlier 18k→2.5k — that was too dark. ---
        const float fc = juce::jmax(3500.0f, 18000.0f - drive * 13000.0f);
        lpA = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * fc / (float)sr);

        //--- compression: squash BEFORE the clip (the cassette "sit down" feel).
        //      threshold: level where squash starts   1.0 → 0.40 across DRIVE
        //      slope:     hardness above it (0 = 1:1)  0   → 0.55 (≈2.2:1)  ---
        compThresh = 1.0f - drive * 0.60f;
        compSlope  = drive * 0.55f;

        //--- follower: ~10 ms in, ~120 ms out (program-style release) ---
        envAtk = 1.0f - std::exp(-1.0f / (0.010f * (float)sr));
        envRel = 1.0f - std::exp(-1.0f / (0.120f * (float)sr));

        //--- gain-compensation follower: ~250 ms (slow = transparent loudness) ---
        lvlCoeff = 1.0f - std::exp(-1.0f / (0.250f * (float)sr));

        //--- head bump: playback-head low lift, 0 → +3.5 dB at 90 Hz across DRIVE ---
        auto bumpC = juce::IIRCoefficients::makePeakFilter(
            sr, 90.0, 0.7, juce::Decibels::decibelsToGain(drive * 3.5f));
        bump[0].setCoefficients(bumpC);
        bump[1].setCoefficients(bumpC);
    }

    //--- process one (stereo) sample in place.  setDrive(drive,..) must have run
    //    this block.  stereo == false leaves R untouched.  preGain drives the
    //    shaper/compressor harder for more grit per knob unit WITHOUT changing the
    //    matched loudness (the gain comp below tracks the ORIGINAL input) — used by
    //    the global path so its 100% reaches the per-tap 100%.  preGain 1 = no-op. ---
    void process(float& L, float& R, float drive, bool stereo, float preGain = 1.0f) noexcept
    {
        //--- ORIGINAL pre-stage level (stereo-linked) for gain compensation,
        //    measured BEFORE the pre-gain so loudness is matched to the real input ---
        const float inAbs = stereo ? juce::jmax(std::abs(L), std::abs(R)) : std::abs(L);
        inLvl += lvlCoeff * (inAbs - inLvl);

        //--- pre-gain into the stage (1 = no-op for the per-tap path) ---
        L *= preGain;
        if (stereo) R *= preGain;

        //--- compression: one stereo-linked follower rides the louder channel so
        //    the image doesn't wander; gain dips smoothly above the threshold ---
        const float drvAbs = stereo ? juce::jmax(std::abs(L), std::abs(R)) : std::abs(L);
        env += (drvAbs > env ? envAtk : envRel) * (drvAbs - env);
        float cg = 1.0f;
        if (env > compThresh)
            cg = std::pow(env / compThresh, -compSlope);
        L *= cg;
        if (stereo) R *= cg;

        //--- soft-clip → head bump → high-cut ---
        L = tapeShape(L, drive);
        L = bump[0].processSingleSampleRaw(L);
        lp[0] += lpA * (L - lp[0]); L = lp[0];
        if (stereo)
        {
            R = tapeShape(R, drive);
            R = bump[1].processSingleSampleRaw(R);
            lp[1] += lpA * (R - lp[1]); R = lp[1];
        }

        //--- auto gain compensation: match output loudness back to the input so
        //    pushing DRIVE adds colour, not level (feed-forward → stable) ---
        const float outAbs = stereo ? juce::jmax(std::abs(L), std::abs(R)) : std::abs(L);
        outLvl += lvlCoeff * (outAbs - outLvl);
        const float gc = (outLvl > 1.0e-6f)
            ? juce::jlimit(0.25f, 4.0f, inLvl / outLvl) : 1.0f;
        L *= gc;
        if (stereo) R *= gc;
    }
};

//==============================================================================
// PitchShifter — per-tap WSOLA (waveform-similarity overlap-add) pitch shifter.
// Cleaner than naive granular: two Hann grains at 50% overlap, and at every grain
// boundary the new grain's start is correlation-aligned to the outgoing grain so
// the splice is phase-coherent (this is what kills the granular "buzz").  Reads
// straight from the delay buffer, so it adds NO latency to the dry.
//   • Pitch is EXACT by construction — each grain plays the buffer at the resample
//     ratio; WSOLA only decides WHERE a grain starts, so it can't detune.
//   • Timing state is shared across L/R so the stereo image holds.
//   • The grain start is re-anchored to the tap delay every hop, so the echo never
//     drifts off its beat.
//==============================================================================
struct PitchShifter
{
    //--- overlap factor: 4 = 75% overlap (smoother OLA than 50%/2-grain) ---
    static constexpr int kOverlap = 4;

    bool  primed = false;
    int   phase  = 0;                  // 0..hop: output position within the current hop
    float off[kOverlap] = { 0,0,0,0 }; // birth offsets of the K active grains (slot 0 = youngest)

    void reset() { primed = false; phase = 0; for (auto& o : off) o = 0.0f; }

    //--- fractional (linear) read, `off` samples back from the write head ---
    static float readLerp(const juce::AudioBuffer<float>& buf, int ch, int wp, int bufSize, float off) noexcept
    {
        off = juce::jlimit(1.0f, (float)bufSize - 2.0f, off);
        float rp = (float)wp - off;
        if (rp < 0.0f) rp += (float)bufSize;
        const int   i0 = (int)rp;
        const int   i1 = (i0 + 1 < bufSize) ? i0 + 1 : 0;
        const float fr = rp - (float)i0;
        return buf.getSample(ch, i0) * (1.0f - fr) + buf.getSample(ch, i1) * fr;
    }

    //--- integer read for the (cheap, decimated) correlation search ---
    static float readInt(const juce::AudioBuffer<float>& buf, int ch, int wp, int bufSize, float off) noexcept
    {
        int o   = juce::jlimit(1, bufSize - 2, (int)(off + 0.5f));
        int idx = wp - o; if (idx < 0) idx += bufSize;
        return buf.getSample(ch, idx);
    }

    //--- one output (stereo) sample.  N = grain length (samples); search = WSOLA
    //    half-range; corrLen = correlation window.  Advances the shifter state. ---
    void process(const juce::AudioBuffer<float>& buf, int wp, int bufSize,
                 float baseOffset, float ratio, int N, int search, int corrLen,
                 bool stereo, float& outL, float& outR) noexcept
    {
        const int   hop   = juce::jmax(1, N / kOverlap);
        const float oneR  = 1.0f - ratio;
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float invN  = 1.0f / (float)N;

        //--- nominal birth offset: centre the grain so the AVERAGE delay stays at
        //    the tap delay (a grain plays content forward at `ratio` over N samples) ---
        const float nominal = baseOffset + (ratio - 1.0f) * (float)N * 0.5f;

        if (!primed) { for (auto& o : off) o = nominal; phase = 0; primed = true; }

        //--- sum the K overlapping Hann grains.  Slot k has age phase + k*hop;
        //    slot 0 is youngest (fading in), slot K-1 oldest (fading out).  Hann at
        //    this overlap is constant-overlap-add = K/2, so divide back to unity. ---
        float accL = 0.0f, accR = 0.0f;
        for (int k = 0; k < kOverlap; ++k)
        {
            const float age = (float)(phase + k * hop);
            const float w   = 0.5f * (1.0f - std::cos(twoPi * age * invN));
            const float oe  = off[k] + oneR * age;
            accL += w * readLerp(buf, 0, wp, bufSize, oe);
            if (stereo) accR += w * readLerp(buf, 1, wp, bufSize, oe);
        }
        const float norm = 2.0f / (float)kOverlap;
        outL = accL * norm;
        outR = stereo ? accR * norm : outL;

        if (++phase >= hop)
        {
            phase = 0;
            //--- retire the oldest grain, shift the rest down a slot ---
            for (int k = kOverlap - 1; k >= 1; --k) off[k] = off[k - 1];

            //--- WSOLA: choose the new grain start (near nominal, so the delay is
            //    held) that best correlates with the previous grain's content at the
            //    overlap → a phase-coherent splice instead of a buzzy one ---
            const float prevAnchor = off[1] + oneR * (float)hop;
            const int   cl    = juce::jmax(8, juce::jmin(corrLen, N - hop));
            const int   kStep = 4, cStep = 3;
            float bestScore = -1.0e30f, bestOff = nominal;
            for (int d = -search; d <= search; d += cStep)
            {
                const float cand = nominal + (float)d;
                float dot = 0.0f, eng = 0.0f;
                for (int k = 0; k < cl; k += kStep)
                {
                    const float a = readInt(buf, 0, wp, bufSize, cand       + oneR * (float)k);
                    const float b = readInt(buf, 0, wp, bufSize, prevAnchor + oneR * (float)k);
                    dot += a * b;  eng += a * a;
                }
                const float score = dot / std::sqrt(eng + 1.0e-9f);  // normalised
                if (score > bestScore) { bestScore = score; bestOff = cand; }
            }
            off[0] = bestOff;
        }
    }
};

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

    //--- reverse shaping (only used when reverse == true).  Appended AFTER the
    //    existing fields so the positional EchoNode{...} inits in the processor
    //    still match by position (new fields fall back to these defaults). ---
    float reverseLength = 1.0f;   // 0..1: length of the reversed segment
    bool  reverseLock   = true;   // true = attack stays on the beat (length ≤ ½ tap);
                                  // false = length runs free (attack can drift late)

    //--- per-tap pitch shift in semitones (0 = unison).  Drawn on the PITCH layer;
    //    forward taps only.  Snapped to whole semitones in the UI. ---
    float pitchSemitones = 0.0f;

    bool operator==(const EchoNode& o) const
    {
        return std::memcmp(&positionBeats,  &o.positionBeats,  sizeof(float)) == 0
            && std::memcmp(&gain,           &o.gain,           sizeof(float)) == 0
            && std::memcmp(&pan,            &o.pan,            sizeof(float)) == 0
            && std::memcmp(&probability,    &o.probability,    sizeof(float)) == 0
            && std::memcmp(&saturation,     &o.saturation,     sizeof(float)) == 0
            && std::memcmp(&reverseLength,  &o.reverseLength,  sizeof(float)) == 0
            && std::memcmp(&pitchSemitones, &o.pitchSemitones, sizeof(float)) == 0
            && active == o.active && reverse == o.reverse
            && reverseLock == o.reverseLock;
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

    //--- tape / saturation (editor-writable, audio-thread-read).  Per-tap grit
    //    lives in each EchoNode.saturation; these are the GLOBAL tape stage that
    //    the whole output (dry + echoes) passes through. ---
    float satDrive          = 0.0f;   // global tape drive, 0 = clean (off)
    bool  satGlobalOverride = false;  // ignore per-tap grit; drive everything with satDrive

    //--- global input / output trim (linear gain, editor-writable, audio-thread-read).
    //    inputGain scales the signal entering the whole effect (dry + what's recorded
    //    into the delay buffer, so it also sets how hard saturation is driven);
    //    outputGain is the final level after everything.  1.0 = unity (0 dB). ---
    float inputGain  = 1.0f;
    float outputGain = 1.0f;

    //--- per-tap WSOLA pitch-shift frame length in ms (editor-writable, audio-read).
    //    Ear-tuned default 30 ms.  SHORTER = smoother (the OLA repeat/skip happens
    //    faster and blends); LONGER = choppier (slower repeat, audible roughness).
    //    Too short eventually can't lock onto low notes. ---
    float pitchGrainMs = 30.0f;

private:
    //--- per-node DSP state (audio thread only).  Reverse needs no per-node
    //    state any more: it's a stateless function of the transport position. ---
    struct NodeState
    {
        bool      fired        = true;
        float     gainEnvelope = 1.0f;
        float     timingJitter = 0.0f;
        TapeStage    tape;       // per-tap SAT: same tape stage as the global drive
        PitchShifter pitch;      // per-tap WSOLA pitch shifter (forward taps only)
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

    //--- the GLOBAL tape stage the whole output (dry + echoes) passes through.
    //    Identical DSP to each tap's per-tap TapeStage. ---
    TapeStage globalTape;

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
