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

enum class TraceOrigin {
    None,
    File,        // measured by an external VNA (Touchstone/CSV import)
    ScalarSweep  // SDR++ receiver + external broadband source
};

// Whether a fit can be trusted given how the trace was measured. A fit that is not validated is still shown,
// marked as such, with the first reason that failed.
struct FitValidation {
    bool validated = false;
    std::string reason;
};

// Thru must be at least this far above the noise floor for a scalar sweep fit to count as validated
constexpr float kMinDynamicRangeDb = 20.0f;
// The -3 dB width must span at least this many trace points
constexpr size_t kMinPointsIn3dbWidth = 3;

FitValidation validateFit(const std::vector<VNASweepPoint>& trace, const ResonatorDipFit& fit, TraceOrigin origin);

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
    void setTraceSource(const std::string& source, TraceOrigin origin);
    std::string getTraceSource() const;
    TraceOrigin getTraceOrigin() const;

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
    TraceOrigin traceOrigin_ = TraceOrigin::None;
};

} // namespace vna
} // namespace rf_tools
