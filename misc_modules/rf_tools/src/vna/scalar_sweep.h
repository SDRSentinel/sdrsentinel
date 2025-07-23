#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "vna_analyzer.h"

namespace rf_tools {
namespace vna {

// What a scalar sweep needs from the receiver. The module implements it with an SDR++ VFO; tests use a fake.
class ScalarProbe {
public:
    virtual ~ScalarProbe() = default;

    // Move the measurement frequency (Hz of the signal being measured) to hz
    virtual bool tune(double hz) = 0;

    // Discard samples for settleSec after the last tune, then average |x|^2 over dwellSec. Full-scale-relative dB.
    virtual bool measurePowerDb(double settleSec, double dwellSec, float& outDb) = 0;

    // Called from another thread to make a blocked measurePowerDb return false
    virtual void requestCancel() {}

    // Called once when the sweep ends for any reason (restore the receiver state)
    virtual void finish() {}

    virtual std::string lastError() const { return ""; }
};

struct ScalarSweepSettings {
    double startHz = 98.0e6;
    double stopHz = 102.0e6;
    int points = 201;
    double rbwHz = 10.0e3;      // bandwidth of the measurement VFO
    double offsetHz = 250.0e3; // VFO offset from the source center, keeps the DC spike/LO leakage out of the VFO
    double settleSec = 0.050;
    double dwellSec = 0.020;

    // Settings that make a stored reference unusable when they change
    bool sameGrid(const ScalarSweepSettings& o) const {
        return startHz == o.startHz && stopHz == o.stopHz && points == o.points && rbwHz == o.rbwHz && offsetHz == o.offsetHz;
    }
};

enum class SweepStage {
    Thru,  // cable without the DUT: reference power per point
    Dut,   // with the DUT: S21 = P_dut - P_thru
    Noise  // source off: noise floor per point
};

struct ScalarSweepResult {
    SweepStage stage = SweepStage::Thru;
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::vector<VNASweepPoint> trace; // only for SweepStage::Dut
    ScalarSweepSettings settings;
    std::chrono::system_clock::time_point finishedAt;
};

class ScalarSweep {
public:
    static constexpr double kMaxDurationSec = 600.0;
    static constexpr float kUnreliableMarginDb = 3.0f; // DUT reading closer than this to the floor is unreliable

    ScalarSweep() = default;
    ~ScalarSweep();
    ScalarSweep(const ScalarSweep&) = delete;
    ScalarSweep& operator=(const ScalarSweep&) = delete;

    // points * (settle + dwell); retune and stream latency come on top
    static double estimateSeconds(const ScalarSweepSettings& s);
    // Empty string when the settings are usable
    static std::string validate(const ScalarSweepSettings& s);
    static std::string describe(const ScalarSweepSettings& s);

    // Blocking sweep on the calling thread (the worker thread uses this, tests call it directly)
    ScalarSweepResult run(SweepStage stage, const ScalarSweepSettings& s, ScalarProbe& probe);

    // Runs on its own thread; the probe is owned until pollFinished() collects the result. Returns an error text or "".
    std::string start(SweepStage stage, const ScalarSweepSettings& s, std::unique_ptr<ScalarProbe> probe);
    void cancel();
    bool isRunning() const { return running_.load(); }
    // 0..1 of the current sweep
    float progress() const;
    // Once per finished sweep: joins the thread, destroys the probe on the calling thread and returns the result
    bool pollFinished(ScalarSweepResult& out);

    // Thru and noise floor are dropped when the grid, RBW or offset differ from the settings they were measured with
    void invalidateIfChanged(const ScalarSweepSettings& current);
    bool hasThru() const;
    bool hasNoise() const;
    std::chrono::system_clock::time_point thruTime() const;
    std::chrono::system_clock::time_point noiseTime() const;
    // Empty when a sweep of this stage can start with these settings
    std::string checkPrerequisites(SweepStage stage, const ScalarSweepSettings& s) const;

private:
    ScalarSweepResult runImpl(SweepStage stage, const ScalarSweepSettings& s, ScalarProbe& probe);

    mutable std::mutex mutex_;
    bool haveThru_ = false, haveNoise_ = false;
    ScalarSweepSettings thruSettings_, noiseSettings_;
    std::vector<float> thruDb_, noiseDb_;
    std::chrono::system_clock::time_point thruTime_, noiseTime_;

    std::atomic<bool> cancel_{ false };
    std::atomic<bool> running_{ false };
    std::atomic<bool> finished_{ false };
    std::atomic<int> done_{ 0 };
    std::atomic<int> total_{ 0 };
    std::atomic<ScalarProbe*> activeProbe_{ nullptr };

    std::thread thread_;
    std::unique_ptr<ScalarProbe> ownedProbe_;
    ScalarSweepResult asyncResult_;
};

} // namespace vna
} // namespace rf_tools
