// Offline verification for the frequency-domain FormantShifter.
//   1. ratio 1.0  → output reconstructs the input (STFT/overlap-add is unity).
//   2. ratio 2.0  → the spectral envelope (formants) moves UP appreciably.
//   3. ratio 2.0  → the fundamental (pitch) is preserved (autocorrelation at f0).
// Builds a FormantContext just like the processor's prepareToPlay does, drives a
// synthetic vowel (buzz at f0 shaped by two formants), and measures the result.
#include "../Source/PluginProcessor.h"
#include <iostream>
#include <vector>
#include <cmath>

static constexpr double kFs = 48000.0;
static constexpr double kF0 = 120.0;      // fundamental (pitch)

//--- two-formant magnitude envelope (Lorentzian peaks, no broadband floor so the
//    spectrum IS the formants and a move is visible in the centroid) ---
static float vowelEnv(double f)
{
    auto peak = [&](double fc, double bw) { double x = (f - fc) / bw; return 1.0 / (1.0 + x * x); };
    return (float)(peak(700.0, 120.0) + 0.8 * peak(1200.0, 150.0));
}

//--- spectral centroid (Hz) over [lo,hi], Hann-windowed, via an order-12 FFT ---
static double centroid(const std::vector<float>& sig, int start, double lo, double hi)
{
    const int order = 12, M = 1 << order;            // 4096
    juce::dsp::FFT fft(order);
    std::vector<std::complex<float>> in(M), out(M);
    for (int i = 0; i < M; ++i)
    {
        float w = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi * i / M));
        in[i] = { sig[start + i] * w, 0.0f };
    }
    fft.perform(in.data(), out.data(), false);
    double num = 0.0, den = 0.0;
    for (int k = 1; k < M / 2; ++k)
    {
        double hz = k * kFs / M;
        if (hz < lo || hz > hi) continue;
        double mag = std::abs(out[k]);
        num += hz * mag; den += mag;
    }
    return den > 0.0 ? num / den : 0.0;
}

//--- RMS log-magnitude difference (dB) between two signals over [lo,hi]: a measure
//    of how aggressively the formant shift reshapes the spectrum (= "drasticness") ---
static double logSpecDist(const std::vector<float>& a, const std::vector<float>& b, int start, double lo, double hi)
{
    const int order = 12, M = 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<std::complex<float>> ia(M), oa(M), ib(M), ob(M);
    for (int i = 0; i < M; ++i)
    {
        float w = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi * i / M));
        ia[i] = { a[start + i] * w, 0.0f }; ib[i] = { b[start + i] * w, 0.0f };
    }
    fft.perform(ia.data(), oa.data(), false);
    fft.perform(ib.data(), ob.data(), false);
    double sum = 0.0; int cnt = 0;
    for (int k = 1; k < M / 2; ++k)
    {
        double hz = k * kFs / M;
        if (hz < lo || hz > hi) continue;
        double da = 20.0 * std::log10(std::abs(oa[k]) + 1e-9);
        double db = 20.0 * std::log10(std::abs(ob[k]) + 1e-9);
        sum += (da - db) * (da - db); ++cnt;
    }
    return cnt ? std::sqrt(sum / cnt) : 0.0;
}

//--- normalised autocorrelation at the f0 lag (1.0 = perfectly periodic at f0) ---
static double pitchCorr(const std::vector<float>& sig, int start, int len)
{
    int lag = (int)std::lround(kFs / kF0);
    double num = 0.0, e = 0.0;
    for (int n = 0; n < len; ++n) { num += sig[start + n] * sig[start + n + lag]; e += sig[start + n] * sig[start + n]; }
    return e > 0.0 ? num / e : 0.0;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    //--- build the same FFT workspace the processor builds in prepareToPlay ---
    const int Nf = FormantShifter::N, Hf = FormantShifter::H;
    juce::dsp::FFT fft(FormantShifter::kOrder);
    std::vector<std::complex<float>> spec(Nf), work(Nf), ceps(Nf);
    std::vector<float> ana(Nf), syn(Nf), env(Nf / 2 + 1);
    for (int i = 0; i < Nf; ++i)
    {
        float hann = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi * i / Nf));
        ana[i] = syn[i] = std::sqrt(hann);
    }
    float cola = 0.0f;
    for (int m = 0; m * Hf < Nf; ++m) { int j = (Nf / 2 + m * Hf) % Nf; cola += ana[j] * syn[j]; }

    FormantContext ctx;
    ctx.fft = &fft; ctx.ana = ana.data(); ctx.syn = syn.data(); ctx.norm = 1.0f / cola;
    ctx.spec = spec.data(); ctx.work = work.data(); ctx.ceps = ceps.data(); ctx.env = env.data();
    ctx.N = Nf; ctx.lifter = juce::jmax(8, Nf / 12); ctx.drama = 1.0f;

    //--- synthesize the vowel ---
    const int total = 48000;
    std::vector<float> input(total);
    for (int n = 0; n < total; ++n)
    {
        double t = n / kFs, s = 0.0;
        for (int m = 1; m * kF0 < kFs * 0.5; ++m) s += vowelEnv(m * kF0) * std::sin(juce::MathConstants<double>::twoPi * m * kF0 * t);
        input[n] = (float)(0.2 * s);
    }

    auto run = [&](const std::vector<float>& in, float ratio)
    {
        FormantShifter fs; fs.reset();
        std::vector<float> out(in.size());
        for (int n = 0; n < (int)in.size(); ++n) out[n] = fs.process(in[n], ratio, ctx);
        return out;
    };

    const int ana0 = 20000;                       // well past settling
    const int corrLen = 8000;

    //--- 1. reconstruction at ratio 1.0 on NOISE (unambiguous delay): scan delays to
    //    find the best alignment → reports the true latency and the residual error ---
    std::vector<float> noise(total);
    juce::Random rng(1);
    for (int n = 0; n < total; ++n) noise[n] = rng.nextFloat() * 2.0f - 1.0f;
    auto oN = run(noise, 1.0f);
    int bestD = 0; double reconErr = 1.0e9;
    for (int D = FormantShifter::N - 64; D <= FormantShifter::N + 64; ++D)
    {
        double err = 0.0, ref = 0.0;
        for (int n = ana0; n < ana0 + corrLen; ++n) { double d = oN[n] - noise[n - D]; err += d * d; ref += noise[n - D] * (double)noise[n - D]; }
        double e = std::sqrt(err / ref);
        if (e < reconErr) { reconErr = e; bestD = D; }
    }

    //--- 2 & 3. formant move + pitch hold at the new full-scale (ratio 2.0 = +12 st ×
    //    1.0 range = one octave) — confirms pitch survives and the move is clear but
    //    musical.  Centroid over the formant band only, so harmonics don't drown it. ---
    const float testRatio = 2.0f;
    auto o2 = run(input, testRatio);
    double cIn  = centroid(input, ana0, 200.0, 3500.0);
    double cOut = centroid(o2,    ana0, 200.0, 3500.0);
    double pIn  = pitchCorr(input, ana0, corrLen);
    double pOut = pitchCorr(o2,    ana0, corrLen);
    double lsd  = logSpecDist(input, o2, ana0, 200.0, 3500.0);

    std::cout.precision(4);
    std::cout << std::fixed;
    std::cout << "[1] recon error (ratio 1.0):     " << reconErr * 100.0 << " %  at delay " << bestD
              << " (N=" << FormantShifter::N << ")   (want < 15%)\n";
    std::cout << "[2] formant centroid in -> out:  " << cIn << " Hz -> " << cOut << " Hz   (ratio "
              << cOut / cIn << ", want > 1.3)\n";
    std::cout << "[3] pitch autocorr in / out:     " << pIn << " / " << pOut << "   (want out > 0.6)\n";
    std::cout << "[*] spectral reshaping in->out:  " << lsd << " dB RMS  (higher = more drastic)\n";

    //--- A/B: confirm the lifter/drama knobs make the effect more drastic ---
    auto lsdFor = [&](int lifter, float drama)
    {
        FormantContext c2 = ctx; c2.lifter = lifter; c2.drama = drama;
        FormantShifter fs; fs.reset();
        std::vector<float> o(total);
        for (int n = 0; n < total; ++n) o[n] = fs.process(input[n], 2.0f, c2);
        return logSpecDist(input, o, ana0, 200.0, 3500.0);
    };
    std::cout << "    A/B old (lifter N/24, drama 1.0): " << lsdFor(Nf / 24, 1.0f) << " dB\n";
    std::cout << "    A/B new (lifter N/12, drama 1.5): " << lsdFor(Nf / 12, 1.5f) << " dB\n";

    bool pass = reconErr < 0.15 && (cOut / cIn) > 1.3 && pOut > 0.6;
    std::cout << (pass ? "\nPASS\n" : "\nFAIL\n");
    return pass ? 0 : 1;
}
