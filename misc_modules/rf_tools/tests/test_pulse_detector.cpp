// Pulse width and noise window checks for PulseDetector (ROADMAP 9.3)
#include "pdw/pulse_detector.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace rf_tools;

static const int PULSE_LEN = 40;
static const int PULSE_AT = 6000;
static const int TOTAL = 8000;

static std::vector<dsp::complex_t> makeSignal(bool withPulse) {
    std::vector<dsp::complex_t> x(TOTAL);
    for (int n = 0; n < TOTAL; ++n) {
        // Constant-magnitude noise with a deterministic rotating phase
        float ph = 0.7f * n;
        float amp = (withPulse && n >= PULSE_AT && n < PULSE_AT + PULSE_LEN) ? 0.5f : 1.0e-3f;
        x[n] = { amp * std::cos(ph), amp * std::sin(ph) };
    }
    return x;
}

int main() {
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) fails++;
    };

    std::vector<RawPulse> pulses;
    PulseDetector det;
    det.init(2.0e6, -40.0f, 2.0e-6, 1000.0e-6);
    det.setAutoThreshold(true);
    det.setPulseCallback([&](const RawPulse& p) { pulses.push_back(p); });
    auto sig = makeSignal(true);
    det.process(sig.data(), (int)sig.size());

    PulseDetector ref;
    ref.init(2.0e6, -40.0f, 2.0e-6, 1000.0e-6);
    ref.setAutoThreshold(true);
    auto noiseOnly = makeSignal(false);
    ref.process(noiseOnly.data(), (int)noiseOnly.size());

    check(pulses.size() == 1, "exactly one pulse detected");
    if (pulses.size() == 1) {
        const RawPulse& p = pulses[0];
        float thr = std::pow(10.0f, det.getEffectiveThresholdDb() / 20.0f);
        check((int)p.samples.size() == PULSE_LEN, "pulse length equals N samples");
        check(p.startSample == (uint64_t)PULSE_AT, "start sample is the rising edge");
        check(p.endSample == (uint64_t)(PULSE_AT + PULSE_LEN - 1), "end sample is the last sample above threshold");
        bool allAbove = true;
        for (const auto& s : p.samples) allAbove &= (std::hypot(s.re, s.im) >= thr);
        check(allAbove, "no sample below the threshold in the pulse");
        bool noiseClean = !p.noiseSamples.empty();
        for (const auto& s : p.noiseSamples) noiseClean &= (std::hypot(s.re, s.im) < thr);
        check(noiseClean, "pre-pulse noise window has no sample above threshold");
    }
    check(std::fabs(det.getNoiseFloorDb() - ref.getNoiseFloorDb()) < 1.0e-6f, "noise floor unchanged by the pulse edge");

    std::printf("exit=%d\n", fails ? 1 : 0);
    return fails ? 1 : 0;
}
