#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <cmath>

namespace rf_tools {
namespace vna {

struct VNASweepPoint {
    double freqHz = 0.0;
    float magnitudeDb = -100.0f;
    float phaseDeg = 0.0f;       // NaN when the phase was not measured (scalar sweep)
    bool valid = true;           // false: unreliable point (e.g. near the noise floor), excluded from the fit
    float dynRangeDb = NAN;      // thru - noise floor at this point, NaN when no noise floor was measured
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
    VNAAnalyzer() = default;
    ~VNAAnalyzer() = default;

    void setConfig(const VNASweepConfig& config);
    VNASweepConfig getConfig() const;

    void clearTrace();
    void addPoint(double freqHz, float magnitudeDb, float phaseDeg);
    void setFullTrace(const std::vector<VNASweepPoint>& trace);

    // Where the trace came from (file name and time, or sweep settings), shown next to the fit
    void setTraceSource(const std::string& source);
    std::string getTraceSource() const;

    std::vector<VNASweepPoint> getTrace() const;
    ResonatorDipFit getDipFit() const;

    // Resonator dip / transmission peak estimate from |S21| in dB only (phase, cable delay, asymmetry and
    // nonlinearity are ignored). Points with valid == false or a non-finite magnitude are skipped: -3 dB style width gives Q_L, then Q_i and Q_c are derived from the dip depth.
    // This is not a complex S21 fit.
    ResonatorDipFit fitResonance(const std::vector<VNASweepPoint>& trace, bool findDip = true) const;

private:
    mutable std::mutex mutex_;
    VNASweepConfig config_;
    std::vector<VNASweepPoint> trace_;
    ResonatorDipFit latestFit_;
    std::string traceSource_;
};

} // namespace vna
} // namespace rf_tools
