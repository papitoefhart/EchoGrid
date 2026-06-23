#include "../Source/PluginProcessor.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <functional>

//==============================================================================
// Mock PlayHead: advances ppqPosition correctly with each block
//==============================================================================
class MockPlayHead : public juce::AudioPlayHead
{
public:
    double bpm_         = 120.0;
    double ppqPos_      = 0.0;    // current beat position
    double sampleRate_  = 4000.0;
    int    blockSize_   = 64;
    double barBeats_    = 4.0;    // bar length in beats (4/4)

    void advanceBlock()
    {
        ppqPos_ += (double)blockSize_ / sampleRate_ * (bpm_ / 60.0);
    }

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(bpm_);
        info.setIsPlaying(true);
        info.setPpqPosition(ppqPos_);
        info.setPpqPositionOfLastBarStart(std::floor(ppqPos_ / barBeats_) * barBeats_);
        info.setTimeSignature(juce::AudioPlayHead::TimeSignature{ 4, 4 });
        return info;
    }
};

//==============================================================================
// Drive the REAL processor with the dry muted and a single reverse tap.  Input
// is supplied by input(globalSample).  Returns the full output (channel 0) and
// the exact input that was fed, both indexed by global sample, so a test can
// compare the reverse output against the input it should mirror.
//==============================================================================
struct RunResult { std::vector<float> out, in; };

static RunResult runReverse(double sampleRate, int blockSize, float positionBeats,
                            long totalSamples, const std::function<float(long)>& input,
                            float revLength = 1.0f, bool revLock = true)
{
    EchoGridProcessor proc;
    proc.prepareToPlay(sampleRate, blockSize);
    proc.dry.gain = 0.0f;
    proc.gridLengthBeats = 4.0f;
    proc.nodes.clear();
    EchoNode n; n.positionBeats = positionBeats; n.gain = 1.0f; n.pan = 0.0f;
    n.probability = 1.0f; n.saturation = 0.0f; n.active = true; n.reverse = true;
    n.reverseLength = revLength; n.reverseLock = revLock;
    proc.nodes.push_back(n);

    MockPlayHead head; head.bpm_ = 120.0; head.sampleRate_ = sampleRate;
    head.blockSize_ = blockSize; head.barBeats_ = 4.0; head.ppqPos_ = 0.0;
    proc.setPlayHead(&head);

    RunResult r;
    r.out.reserve(totalSamples); r.in.reserve(totalSamples);
    juce::AudioBuffer<float> buf(2, blockSize); juce::MidiBuffer midi;

    long g = 0; const long nBlocks = (totalSamples + blockSize - 1) / blockSize;
    for (long b = 0; b < nBlocks; ++b)
    {
        buf.clear();
        for (int s = 0; s < blockSize; ++s)
        {
            float v = input(g + s);
            buf.setSample(0, s, v); buf.setSample(1, s, v);
        }
        proc.processBlock(buf, midi); head.advanceBlock();
        for (int s = 0; s < blockSize; ++s, ++g)
        {
            r.in.push_back(input(g));               // re-evaluate: matches what was fed
            r.out.push_back(buf.getSample(0, s));
        }
    }
    return r;
}

//==============================================================================
// Drive the REAL processor with the dry muted and a single FORWARD tap pitched by
// `semitones`.  Returns the output (channel 0) = the pitched echo only.
//==============================================================================
static std::vector<float> runForwardPitch(double sampleRate, int blockSize,
                                          float positionBeats, float semitones,
                                          long totalSamples,
                                          const std::function<float(long)>& input)
{
    EchoGridProcessor proc;
    proc.prepareToPlay(sampleRate, blockSize);
    proc.dry.gain = 0.0f;                 // echo only
    proc.gridLengthBeats = 4.0f;
    proc.nodes.clear();
    EchoNode n; n.positionBeats = positionBeats; n.gain = 1.0f; n.pan = 0.0f;
    n.probability = 1.0f; n.saturation = 0.0f; n.active = true; n.reverse = false;
    n.pitchSemitones = semitones;
    proc.nodes.push_back(n);

    MockPlayHead head; head.bpm_ = 120.0; head.sampleRate_ = sampleRate;
    head.blockSize_ = blockSize; head.barBeats_ = 4.0; head.ppqPos_ = 0.0;
    proc.setPlayHead(&head);

    std::vector<float> out; out.reserve(totalSamples);
    juce::AudioBuffer<float> buf(2, blockSize); juce::MidiBuffer midi;
    long g = 0; const long nBlocks = (totalSamples + blockSize - 1) / blockSize;
    for (long b = 0; b < nBlocks; ++b)
    {
        buf.clear();
        for (int s = 0; s < blockSize; ++s) { float v = input(g + s); buf.setSample(0, s, v); buf.setSample(1, s, v); }
        proc.processBlock(buf, midi); head.advanceBlock();
        for (int s = 0; s < blockSize; ++s, ++g) out.push_back(buf.getSample(0, s));
    }
    return out;
}

//--- dominant frequency of a steady segment via zero-crossing rate ---
static double zeroCrossFreq(const std::vector<float>& x, long from, long to, double sr)
{
    int crossings = 0;
    for (long i = from + 1; i < to; ++i)
        if ((x[i - 1] <= 0.0f) != (x[i] <= 0.0f)) ++crossings;
    return (double)crossings * 0.5 * sr / (double)(to - from);
}

//==============================================================================
int main()
{
    const double sampleRate = 4000.0;
    const int    blockSize  = 64;
    const double bpm        = 120.0;
    const double spb        = sampleRate * 60.0 / bpm;   // samples per beat
    const long   bar        = (long)std::lround(4.0 * spb);   // pattern = gridLengthBeats(4)
    int failures = 0;

    std::cout << "=== True reverse-delay tests (sr=" << sampleRate
              << ", spb=" << spb << ") ===\n\n";

    //--------------------------------------------------------------------------
    // TEST 1 — Attack lands ON THE BEAT: the loud transient of a reversed note
    // must come back at the TAP DELAY (where a forward echo's attack would be),
    // with the reverse swelling up into it.  The hit is on a PATTERN DOWNBEAT
    // (the reverse is bar-locked, so the downbeat is always a chunk edge).
    //--------------------------------------------------------------------------
    std::cout << "--- Test 1: attack lands at the tap delay ---\n";
    for (float tap : { 0.5f, 1.0f, 1.5f, 2.0f, 3.0f })
    {
        const long t0    = 2 * bar;                                // a pattern downbeat
        const long total = t0 + 2 * bar;
        auto click = [t0](long i) -> float { return (i == t0) ? 1.0f : 0.0f; };
        RunResult r = runReverse(sampleRate, blockSize, tap, total, click);

        long peak = -1; float best = 0.0f;
        for (long g = t0 + 1; g < t0 + bar; ++g)                    // within the bar
        {
            float a = std::fabs(r.out[g]);
            if (a > best) { best = a; peak = g; }
        }
        double delay = (peak < 0) ? -1 : (double)(peak - t0) / spb;
        bool ok = (peak > 0) && std::fabs(delay - tap) < 0.03;
        std::cout << "  tap=" << tap << "b  attack delay=" << delay << "b"
                  << (ok ? "  [OK]" : "  [FAIL]") << "\n";
        if (!ok) ++failures;
    }

    //--------------------------------------------------------------------------
    // TEST 2 — Reverses INTO the attack: audio plays back time-reversed, so a
    // marker played LATER than the attack must come out EARLIER (the swell leads
    // up to the attack).  Feed a loud click (attack) then a quieter marker click
    // a little later; the marker's output peak must precede the attack's, and the
    // attack must still land at the tap delay.
    //--------------------------------------------------------------------------
    std::cout << "\n--- Test 2: reverses into the attack (later input -> earlier output) ---\n";
    for (float tap : { 0.5f, 1.0f, 1.5f, 2.0f })
    {
        const long G     = (long)std::lround((double)tap * 0.5 * spb);  // reversed chunk
        const long t0    = 2 * bar;                                     // a pattern downbeat
        const long delta = G / 2;                                       // marker after attack
        const long total = t0 + 2 * bar;
        auto sig = [t0, delta](long i) -> float {
            if (i == t0)         return 1.0f;    // attack
            if (i == t0 + delta) return 0.7f;    // later marker
            return 0.0f;
        };
        RunResult r = runReverse(sampleRate, blockSize, tap, total, sig);

        //--- attack = global peak; marker = peak before it ---
        long gA = -1; float bA = 0.0f;
        for (long g = t0 + 1; g < t0 + bar; ++g)
        { float a = std::fabs(r.out[g]); if (a > bA) { bA = a; gA = g; } }

        long gB = -1; float bB = 0.0f;
        for (long g = t0 + 1; g < gA - 8; ++g)
        { float a = std::fabs(r.out[g]); if (a > bB) { bB = a; gB = g; } }

        double attackDelay = (gA < 0) ? -1 : (double)(gA - t0) / spb;
        bool ok = (gA > 0) && (gB > 0) && (gB < gA)
                  && std::fabs(attackDelay - tap) < 0.03
                  && std::labs((gA - gB) - delta) < std::max<long>(8, G / 20);
        std::cout << "  tap=" << tap << "b  attack@delay " << attackDelay
                  << "b  marker " << (gA - gB) << " samples earlier (want " << delta << ")"
                  << (ok ? "  [OK]" : "  [FAIL]") << "\n";
        if (!ok) ++failures;
    }

    //--------------------------------------------------------------------------
    // TEST 3 — Bar-to-bar consistency (this is the reported "beat 4 / off-grid"
    // bug).  Feed the SAME pattern-relative hit on the downbeat of consecutive
    // bars; the reverse attack must land at the SAME pattern-relative spot every
    // bar — AND at the tap delay — including tap positions whose chunk (tap/2)
    // does NOT divide the bar (beat 4 = 3b, and the 16ths 3.25/3.5/3.75).
    //--------------------------------------------------------------------------
    std::cout << "\n--- Test 3: reverse lands in the same spot (= tap delay) every bar ---\n";
    for (float tap : { 1.0f, 2.0f, 3.0f, 3.25f, 3.5f, 3.75f })   // beat2,3,4 + 4e,4+,4a
    {
        long firstRel = -1; bool consistent = true;
        for (int b = 2; b <= 5; ++b)
        {
            const long impulse = (long)b * bar;                  // downbeat of bar b
            const long total   = impulse + 2 * bar;
            auto click = [impulse](long i) -> float { return (i == impulse) ? 1.0f : 0.0f; };
            RunResult r = runReverse(sampleRate, blockSize, tap, total, click);

            long peak = -1; float best = 0.0f;
            for (long g = impulse + 1; g < impulse + bar; ++g)   // within the bar
            { float a = std::fabs(r.out[g]); if (a > best) { best = a; peak = g; } }

            long rel = (peak < 0) ? -1 : (peak % bar);
            if (b == 2) firstRel = rel;
            else if (rel < 0 || std::labs(rel - firstRel) > 40) consistent = false;
        }
        //--- the attack should land one tap delay after the downbeat ---
        const long expectRel = (long)std::lround((double)tap * spb);
        bool onTime = (firstRel >= 0) && std::labs(firstRel - expectRel) < 60;
        bool ok = consistent && onTime;
        std::cout << "  tap=" << tap << "b (slider 'beat " << (tap + 1.0f)
                  << "')  reverse @ bar-rel " << firstRel << " (want " << expectRel << ")"
                  << (ok ? "  [OK]" : (consistent ? "  [WRONG SPOT]" : "  [VARIES <-- BUG]")) << "\n";
        if (!ok) ++failures;
    }

    //--------------------------------------------------------------------------
    // TEST 4 — REV LEN knob + IN TIME / FREE toggle.
    //   IN TIME: any length keeps the attack at the tap delay (on the beat).
    //   FREE: the attack lands at 2 x (length x tap) — drifts as length grows.
    //--------------------------------------------------------------------------
    std::cout << "\n--- Test 4a: IN TIME — attack stays at tap delay for any length ---\n";
    for (float tap : { 1.0f, 2.0f, 3.0f })
        for (float len : { 1.0f, 0.5f, 0.25f })
        {
            const long t0 = 2 * bar, total = t0 + 2 * bar;
            auto click = [t0](long i) -> float { return (i == t0) ? 1.0f : 0.0f; };
            RunResult r = runReverse(sampleRate, blockSize, tap, total, click, len, /*lock=*/true);
            long peak = -1; float best = 0.0f;
            for (long g = t0 + 1; g < t0 + bar; ++g)
            { float a = std::fabs(r.out[g]); if (a > best) { best = a; peak = g; } }
            double delay = (peak < 0) ? -1 : (double)(peak - t0) / spb;
            bool ok = (peak > 0) && std::fabs(delay - tap) < 0.05;
            std::cout << "  tap=" << tap << "b len=" << len
                      << "  attack delay=" << delay << "b (want " << tap << ")"
                      << (ok ? "  [OK]" : "  [FAIL]") << "\n";
            if (!ok) ++failures;
        }

    //--- FREE mode is currently disabled in the UI (kShowReverseTimingToggle=false)
    //    but the DSP is kept; this test guards that dormant path stays correct. ---
    std::cout << "\n--- Test 4b: FREE (UI-disabled, code kept) — attack at 2 x (length x tap) ---\n";
    for (float tap : { 0.5f, 1.0f })
        for (float len : { 1.0f, 0.5f })
        {
            const long t0 = 2 * bar, total = t0 + 2 * bar;
            auto click = [t0](long i) -> float { return (i == t0) ? 1.0f : 0.0f; };
            RunResult r = runReverse(sampleRate, blockSize, tap, total, click, len, /*lock=*/false);
            long peak = -1; float best = 0.0f;
            for (long g = t0 + 1; g < t0 + bar; ++g)
            { float a = std::fabs(r.out[g]); if (a > best) { best = a; peak = g; } }
            double delay   = (peak < 0) ? -1 : (double)(peak - t0) / spb;
            double expect  = 2.0 * len * tap;
            bool ok = (peak > 0) && std::fabs(delay - expect) < 0.05;
            std::cout << "  tap=" << tap << "b len=" << len
                      << "  attack delay=" << delay << "b (want " << expect << ")"
                      << (ok ? "  [OK]" : "  [FAIL]") << "\n";
            if (!ok) ++failures;
        }

    //--------------------------------------------------------------------------
    // TEST 5 — PITCH shift accuracy: a forward tap pitched by N semitones must
    // play its echo at the input frequency × 2^(N/12).  Feed a steady sine, mute
    // the dry, and measure the echo's frequency by its zero-crossing rate.
    //--------------------------------------------------------------------------
    std::cout << "\n--- Test 5: WSOLA pitch shift lands on the right frequency ---\n";
    {
        const double f0    = 200.0;                       // input sine (Hz)
        const long   total = 12000;                       // 3 s at sr 4000
        auto sine = [f0, sampleRate](long i) -> float
            { return std::sin(2.0 * juce::MathConstants<double>::pi * f0 * (double)i / sampleRate); };

        for (float semi : { 0.0f, 7.0f, 12.0f, -12.0f })
        {
            auto out = runForwardPitch(sampleRate, blockSize, 1.0f, semi, total, sine);
            double measured = zeroCrossFreq(out, 8000, 11500, sampleRate);
            double expect   = f0 * std::pow(2.0, semi / 12.0);
            double errPct   = 100.0 * std::fabs(measured - expect) / expect;
            bool   ok       = errPct < 6.0;
            std::cout << "  " << (semi >= 0 ? "+" : "") << (int)semi << " st: measured "
                      << (int)std::lround(measured) << " Hz (want " << (int)std::lround(expect)
                      << ")  err " << (int)std::lround(errPct) << "%"
                      << (ok ? "  [OK]" : "  [FAIL]") << "\n";
            if (!ok) ++failures;
        }
    }

    std::cout << "\n=== " << (failures == 0 ? "ALL PASS" : "FAILURES")
              << " (" << failures << " failed) ===\n";
    return failures == 0 ? 0 : 1;
}
