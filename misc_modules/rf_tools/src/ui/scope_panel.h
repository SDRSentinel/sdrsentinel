#pragma once
#include "../mop/mop_analyzer.h"
#include "../pdw/pulse_detector.h"
#include <mutex>
#include <vector>

namespace rf_tools {

class ScopePanel {
public:
    ScopePanel();
    ~ScopePanel() = default;

    void loadPulse(const RawPulse& pulse, double sampleRate, double centerFreqHz);
    void draw();

    bool isFrozen() const { return frozen_; }
    void setFrozen(bool frozen) { frozen_ = frozen; }

private:
    void drawWaveformPlot();
    void drawInstFreqPlot();
    void drawConstellationPlot();
    void drawSTFTSpectrogram();

    mutable std::mutex mutex_;
    RawPulse currentPulse_;
    MOPResult mopResult_;
    double sampleRate_ = 2000000.0;
    double centerFreqHz_ = 0.0;
    uint64_t pulseCounter_ = 0;

    bool hasPulse_ = false;
    bool frozen_ = false;

    // View controls
    int currentTabIdx_ = 0; // 0: Envelope & IQ, 1: Instantaneous Freq, 2: Constellation, 3: Spectrogram
    bool showI_ = true;
    bool showQ_ = true;
    bool showEnvelope_ = true;
    float constellationRadius_ = 100.0f;
};

} // namespace rf_tools
