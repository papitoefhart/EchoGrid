#include "../Source/PluginProcessor.h"
#include <iostream>
#include <vector>
#include <cmath>

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
        info.setPpqPosition(ppqPos_);
        //--- report the downbeat of the current bar, like a real DAW ---
        info.setPpqPositionOfLastBarStart(std::floor(ppqPos_ / barBeats_) * barBeats_);
        info.setTimeSignature(juce::AudioPlayHead::TimeSignature{ 4, 4 });
        return info;
    }
};

//==============================================================================
//==============================================================================
// Bar-consistency test: feed ONE impulse at a chosen global sample (i.e. a chosen
// bar + bar-relative offset) during continuous playback from ppq=0, and return the
// global sample index of the reverse peak.  Mirrors the real DAW scenario.
//==============================================================================
static int reversePeakForImpulseAt(double sampleRate, int blockSize,
                                   float positionBeats, float gridBeats,
                                   long impulseGlobalSample, long totalSamples)
{
    EchoGridProcessor proc;
    proc.prepareToPlay(sampleRate, blockSize);
    proc.dry.gain         = 0.0f;
    proc.gridLengthBeats  = gridBeats;

    proc.nodes.clear();
    EchoNode n;
    n.positionBeats = positionBeats; n.gain = 1.0f; n.pan = 0.0f;
    n.probability = 1.0f; n.saturation = 0.0f; n.active = true; n.reverse = true;
    proc.nodes.push_back(n);

    MockPlayHead head;
    head.bpm_ = 120.0; head.sampleRate_ = sampleRate; head.blockSize_ = blockSize;
    head.barBeats_ = gridBeats; head.ppqPos_ = 0.0;
    proc.setPlayHead(&head);

    juce::AudioBuffer<float> buf(2, blockSize);
    juce::MidiBuffer midi;

    long g = 0;                    // global sample counter
    float bestAbs = 0.0f; long bestIdx = -1;
    const long nBlocks = (totalSamples + blockSize - 1) / blockSize;
    for (long b = 0; b < nBlocks; ++b)
    {
        buf.clear();
        //--- drop the impulse into whatever block contains impulseGlobalSample ---
        if (impulseGlobalSample >= g && impulseGlobalSample < g + blockSize)
        {
            int off = (int)(impulseGlobalSample - g);
            buf.setSample(0, off, 1.0f);
            buf.setSample(1, off, 1.0f);
        }
        proc.processBlock(buf, midi);
        head.advanceBlock();

        for (int s = 0; s < blockSize; ++s, ++g)
        {
            //--- only look AFTER the impulse for the reverse response ---
            if (g <= impulseGlobalSample) continue;
            float a = std::abs(buf.getSample(0, s));
            if (a > bestAbs) { bestAbs = a; bestIdx = g; }
        }
    }
    return (int)bestIdx;
}

//==============================================================================
// Dump the FULL reverse response to one impulse: lists every output burst and its
// bar-relative position, so we can see if one input makes several echoes.
//==============================================================================
static void dumpImpulseResponse(double sampleRate, int blockSize,
                                float positionBeats, float gridBeats,
                                long impulseGlobalSample, long totalSamples)
{
    const double spb        = sampleRate * 60.0 / 120.0;
    const long   barSamples = (long)(gridBeats * spb);

    EchoGridProcessor proc;
    proc.prepareToPlay(sampleRate, blockSize);
    proc.dry.gain = 0.0f; proc.gridLengthBeats = gridBeats;
    proc.nodes.clear();
    EchoNode n; n.positionBeats = positionBeats; n.gain = 1.0f; n.pan = 0.0f;
    n.probability = 1.0f; n.saturation = 0.0f; n.active = true; n.reverse = true;
    proc.nodes.push_back(n);
    MockPlayHead head; head.bpm_ = 120.0; head.sampleRate_ = sampleRate;
    head.blockSize_ = blockSize; head.barBeats_ = gridBeats; head.ppqPos_ = 0.0;
    proc.setPlayHead(&head);

    juce::AudioBuffer<float> buf(2, blockSize); juce::MidiBuffer midi;
    long g = 0; const long nBlocks = (totalSamples + blockSize - 1) / blockSize;
    bool inBurst = false; long burstStart = 0; float burstPeak = 0.0f;
    std::cout << "  impulse at sample " << impulseGlobalSample
              << " (ppq " << (impulseGlobalSample / spb) << ", bar-rel "
              << (impulseGlobalSample % barSamples) << ") -> reverse bursts:\n";
    for (long b = 0; b < nBlocks; ++b)
    {
        buf.clear();
        if (impulseGlobalSample >= g && impulseGlobalSample < g + blockSize)
            { int o = (int)(impulseGlobalSample - g); buf.setSample(0,o,1.0f); buf.setSample(1,o,1.0f); }
        proc.processBlock(buf, midi); head.advanceBlock();
        for (int s = 0; s < blockSize; ++s, ++g)
        {
            float a = (g > impulseGlobalSample) ? std::abs(buf.getSample(0, s)) : 0.0f;
            if (a > 0.05f && !inBurst) { inBurst = true; burstStart = g; burstPeak = a; }
            else if (a > 0.05f) burstPeak = std::max(burstPeak, a);
            else if (inBurst && a <= 0.05f)
            {
                inBurst = false;
                std::cout << "    burst @ sample " << burstStart
                          << "  bar-rel " << (burstStart % barSamples)
                          << "  (ppq " << (burstStart / spb)
                          << ", beat " << (1.0 + std::fmod(burstStart / spb, gridBeats))
                          << ")  peak " << burstPeak << "\n";
            }
        }
    }
}

//==============================================================================
// Realistic test: feed an EXTENDED burst (e.g. an 8th-note-long sound) at the same
// bar-relative spot in consecutive bars and report the reverse ONSET bar-relative.
//==============================================================================
static long reverseOnsetForBurst(double sampleRate, int blockSize,
                                 float positionBeats, float gridBeats,
                                 long burstStartSample, long burstLenSamples,
                                 long totalSamples, float dawBarBeats = -1.0f,
                                 float snapStep = 0.25f, bool reverse = true)
{
    EchoGridProcessor proc;
    proc.prepareToPlay(sampleRate, blockSize);
    proc.dry.gain = 0.0f; proc.gridLengthBeats = gridBeats; proc.snapStepBeats = snapStep;
    proc.nodes.clear();
    EchoNode n; n.positionBeats = positionBeats; n.gain = 1.0f; n.pan = 0.0f;
    n.probability = 1.0f; n.saturation = 0.0f; n.active = true; n.reverse = reverse;
    proc.nodes.push_back(n);
    MockPlayHead head; head.bpm_ = 120.0; head.sampleRate_ = sampleRate;
    head.blockSize_ = blockSize;
    head.barBeats_ = (dawBarBeats > 0.0f) ? dawBarBeats : gridBeats;  // DAW time-sig bar
    head.ppqPos_ = 0.0;
    proc.setPlayHead(&head);

    juce::AudioBuffer<float> buf(2, blockSize); juce::MidiBuffer midi;
    long g = 0; const long nBlocks = (totalSamples + blockSize - 1) / blockSize;
    const long burstEnd = burstStartSample + burstLenSamples;
    for (long b = 0; b < nBlocks; ++b)
    {
        buf.clear();
        for (int s = 0; s < blockSize; ++s)
        {
            long gs = g + s;
            if (gs >= burstStartSample && gs < burstEnd)
            {
                //--- ramp so the reversed copy is easy to localise ---
                float v = 0.3f + 0.7f * (float)(gs - burstStartSample) / (float)burstLenSamples;
                buf.setSample(0, s, v); buf.setSample(1, s, v);
            }
        }
        proc.processBlock(buf, midi); head.advanceBlock();
        for (int s = 0; s < blockSize; ++s, ++g)
        {
            if (g < burstEnd) continue;          // ignore the burst region itself
            if (std::abs(buf.getSample(0, s)) > 0.05f) return g;   // first reverse output
        }
    }
    return -1;
}

int main()
{
    const double sampleRate = 4000.0;
    const int    blockSize  = 64;
    const double bpm        = 120.0;
    const double spb        = sampleRate * 60.0 / bpm;   // samples per beat
    const float  gridBeats  = 4.0f;                       // one 4/4 bar
    const long   barSamples = (long)(gridBeats * spb);

    std::cout << "=== Reverse bar-consistency diagnostic ===\n";
    std::cout << "sampleRate=" << sampleRate << " bpm=" << bpm
              << " samplesPerBeat=" << spb << " bar=" << barSamples << " samples\n";
    std::cout << "Feeding the SAME bar-relative impulse in bars 2..5, measuring the\n"
              << "reverse peak's BAR-RELATIVE position. If correct, all rows match.\n\n";

    //--- test several tap positions: some divide the 4-beat bar, some don't ---
    float taps[]      = { 0.5f, 1.0f, 0.75f, 1.5f };
    float relOffsets[]= { 0.0f, 1.0f };   // bar-relative beat where the impulse is fed

    for (float relOff : relOffsets)
    {
        std::cout << "--- impulse at bar-relative beat " << relOff << " ---\n";
        for (float tap : taps)
        {
            std::cout << "  tap=" << tap << "b : ";
            int firstRel = -1; bool consistent = true;
            for (int bar = 1; bar <= 4; ++bar)   // bars 2..5 (index 1..4)
            {
                long impulse = (long)((bar * gridBeats + relOff) * spb);
                long total   = impulse + (long)(8 * spb);   // 8 beats of tail
                int  peak    = reversePeakForImpulseAt(sampleRate, blockSize,
                                                       tap, gridBeats, impulse, total);
                int  rel     = (peak < 0) ? -1 : (int)(peak % barSamples);
                std::cout << "bar" << (bar + 1) << "=" << rel << "  ";
                if (bar == 1) firstRel = rel;
                else if (rel < 0 || std::abs(rel - firstRel) > 2) consistent = false;
            }
            std::cout << (consistent ? " [CONSISTENT]" : " [VARIES <-- BUG]") << "\n";
        }
        std::cout << "\n";
    }

    //--- full impulse response: how many echoes does ONE input make, and where? ---
    std::cout << "=== Full reverse response to a SINGLE impulse ===\n";
    for (float tap : { 0.5f, 1.5f })
    {
        std::cout << "tap=" << tap << "b:\n";
        dumpImpulseResponse(sampleRate, blockSize, tap, gridBeats,
                            (long)(2 * gridBeats * spb), (long)(2 * gridBeats * spb) + (long)(12 * spb));
    }

    //--- REALISTIC + POSITION: does the reverse land at input + tap delay, like a
    //    forward tap?  44.1kHz / 512-block / 8th-note sample, GRID=1/8 ---
    std::cout << "\n=== Position check: reverse onset vs forward onset (44.1k, GRID=1/8) ===\n";
    std::cout << "(reverse should land at the SAME delay as forward = input + tap)\n";
    const double sr2 = 44100.0; const int bs2 = 512;
    const double spb2 = sr2 * 60.0 / 120.0;
    const long   bar2s = (long)(gridBeats * spb2);
    const long   eighth = (long)(0.5 * spb2);            // 8th note length in samples
    const float  gridStep = 0.5f;                         // GRID = 1/8 note
    for (float tap : { 0.5f, 1.0f, 1.5f, 2.0f })
    {
        long burst = (long)(2 * gridBeats * spb2);        // downbeat of bar 3
        long tail  = burst + (long)(8 * spb2);
        long fOn = reverseOnsetForBurst(sr2, bs2, tap, gridBeats, burst, eighth, tail,
                                        -1.0f, gridStep, /*reverse=*/false);
        long rOn = reverseOnsetForBurst(sr2, bs2, tap, gridBeats, burst, eighth, tail,
                                        -1.0f, gridStep, /*reverse=*/true);
        double fDelay = (fOn < 0) ? -1 : (double)(fOn - burst) / spb2;
        double rDelay = (rOn < 0) ? -1 : (double)(rOn - burst) / spb2;
        bool ok = (fOn > 0 && rOn > 0 && std::abs(fDelay - rDelay) < 0.03);
        std::cout << "  tap=" << tap << "b : forward delay=" << fDelay
                  << "b  reverse delay=" << rDelay << "b"
                  << (ok ? "  [MATCH]" : "  [MISMATCH <-- BUG]") << "\n";
    }

    //--- bar-consistency still holds with the new scheme ---
    std::cout << "\n=== Bar-consistency (new scheme, GRID=1/8) ===\n";
    for (float tap : { 0.5f, 1.0f, 1.5f })
    {
        std::cout << "  tap=" << tap << "b : ";
        long first = -1; bool ok = true;
        for (int bar = 1; bar <= 4; ++bar)
        {
            long burst = (long)(bar * gridBeats * spb2);
            long onset = reverseOnsetForBurst(sr2, bs2, tap, gridBeats, burst, eighth,
                                              burst + (long)(8 * spb2), -1.0f, gridStep, true);
            long rel = (onset < 0) ? -1 : (onset % bar2s);
            std::cout << "bar" << (bar+1) << "=" << rel << "  ";
            if (bar == 1) first = rel; else if (onset < 0 || std::labs(rel - first) > 4) ok = false;
        }
        std::cout << (ok ? " [CONSISTENT]" : " [VARIES <-- BUG]") << "\n";
    }
    return 0;
}
