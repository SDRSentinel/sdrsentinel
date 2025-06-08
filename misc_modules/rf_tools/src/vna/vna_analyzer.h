#pragma once
#include <vector>
#include <string>
#include <mutex>

namespace rf_tools {
namespace vna {

struct VNASweepPoint {
    double freqHz = 0.0;
    float magnitudeDb = -100.0f;
    float phaseDeg = 0.0f;
};

struct ResonatorDipFit {
    bool valid = false;
    double resonantFreqHz = 0.0;
    double bandwidth3dbHz = 0.0;
    float dipDepthDb = 0.0f;
    float baselineDb = 0.0f;
    float loadedQ = 0.0f;     // Q_L: Total loaded quality factor = f0 / BW_3dB
    float internalQ = 0.0f;   // Q_i: Resonator internal quality factor = Q_L / S21_min
    float couplingQ = 0.0f;   // Q_c: Feedline coupling quality factor = Q_L / (1 - S21_min)
};

struct VNASweepConfig {
    double startFreqHz = 90.0e6;
    double stopFreqHz = 110.0e6;
    size_t numPoints = 201;
    bool isSweeping = false;
    bool findDips = true; // true for resonator notch/dip (MKID), false for bandpass peak
};

class VNAAnalyzer {
public:
    VNAAnalyzer();
    ~VNAAnalyzer() = default;

    void setConfig(const VNASweepConfig& config);
    VNASweepConfig getConfig() const;

    void clearTrace();
    void addPoint(double freqHz, float magnitudeDb, float phaseDeg);
    void setFullTrace(const std::vector<VNASweepPoint>& trace);

    std::vector<VNASweepPoint> getTrace() const;
    ResonatorDipFit getDipFit() const;

    // Resonator dip / transmission peak curve fitting
    // Inspired by GPU_SDR pyUSRP USRP_VNA.py fit_resonator
    ResonatorDipFit fitResonance(const std::vector<VNASweepPoint>& trace, bool findDip = true) const;

    // Synthetic sweep generator for hardware-free simulation / testing
    void generateSyntheticResonatorSweep(double f0 = 100.0e6, double q = 2500.0, float depthDb = 25.0f);

private:
    mutable std::mutex mutex_;
    VNASweepConfig config_;
    std::vector<VNASweepPoint> trace_;
    ResonatorDipFit latestFit_;
};

} // namespace vna
} // namespace rf_tools
