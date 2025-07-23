#include "scalar_sweep.h"
#include <cmath>
#include <cstdio>

namespace rf_tools {
namespace vna {

namespace {
double pointFreq(const ScalarSweepSettings& s, int i) {
    return s.startHz + (s.stopHz - s.startHz) * (double)i / (double)(s.points - 1);
}
}

ScalarSweep::~ScalarSweep() {
    cancel();
    if (thread_.joinable()) { thread_.join(); }
}

double ScalarSweep::estimateSeconds(const ScalarSweepSettings& s) {
    return (double)s.points * (s.settleSec + s.dwellSec);
}

std::string ScalarSweep::validate(const ScalarSweepSettings& s) {
    char buf[200];
    if (!(s.startHz > 0.0) || !(s.stopHz > s.startHz)) { return "Stop frequency must be above start frequency"; }
    if (s.points < 5 || s.points > 4001) { return "Points must be between 5 and 4001"; }
    if (!(s.rbwHz >= 100.0)) { return "RBW must be at least 100 Hz"; }
    if (!(std::fabs(s.offsetHz) >= s.rbwHz)) { return "Offset must be at least as large as the RBW (it keeps the DC spike out of the VFO)"; }
    if (!(s.startHz - s.offsetHz > 0.0)) { return "Offset would put the receiver center below 0 Hz"; }
    if (!(s.settleSec >= 0.0) || !(s.dwellSec > 0.0)) { return "Settle must be >= 0 and dwell must be > 0"; }
    double est = estimateSeconds(s);
    if (est > kMaxDurationSec) {
        std::snprintf(buf, sizeof(buf), "Sweep would take about %.0f s, the limit is %.0f s: reduce points, settle or dwell", est, kMaxDurationSec);
        return buf;
    }
    return "";
}

std::string ScalarSweep::describe(const ScalarSweepSettings& s) {
    char buf[200];
    std::snprintf(buf, sizeof(buf), "%.4f-%.4f MHz, %d pts, RBW %.1f kHz, offset %+.1f kHz, settle %.0f ms, dwell %.0f ms",
                  s.startHz / 1e6, s.stopHz / 1e6, s.points, s.rbwHz / 1e3, s.offsetHz / 1e3, s.settleSec * 1e3, s.dwellSec * 1e3);
    return buf;
}

bool ScalarSweep::hasThru() const { std::lock_guard<std::mutex> l(mutex_); return haveThru_; }
bool ScalarSweep::hasNoise() const { std::lock_guard<std::mutex> l(mutex_); return haveNoise_; }
std::chrono::system_clock::time_point ScalarSweep::thruTime() const { std::lock_guard<std::mutex> l(mutex_); return thruTime_; }
std::chrono::system_clock::time_point ScalarSweep::noiseTime() const { std::lock_guard<std::mutex> l(mutex_); return noiseTime_; }

void ScalarSweep::invalidateIfChanged(const ScalarSweepSettings& current) {
    std::lock_guard<std::mutex> l(mutex_);
    if (haveThru_ && !thruSettings_.sameGrid(current)) { haveThru_ = false; thruDb_.clear(); }
    if (haveNoise_ && !noiseSettings_.sameGrid(current)) { haveNoise_ = false; noiseDb_.clear(); }
}

std::string ScalarSweep::checkPrerequisites(SweepStage stage, const ScalarSweepSettings& s) const {
    std::string err = validate(s);
    if (!err.empty()) { return err; }
    if (stage == SweepStage::Dut) {
        std::lock_guard<std::mutex> l(mutex_);
        if (!haveThru_) { return "Measure thru first (the reference is missing or was invalidated by a settings change)"; }
        if (!thruSettings_.sameGrid(s)) { return "Settings differ from the thru measurement: measure thru again"; }
    }
    return "";
}

ScalarSweepResult ScalarSweep::run(SweepStage stage, const ScalarSweepSettings& s, ScalarProbe& probe) {
    cancel_.store(false);
    return runImpl(stage, s, probe);
}

ScalarSweepResult ScalarSweep::runImpl(SweepStage stage, const ScalarSweepSettings& s, ScalarProbe& probe) {
    ScalarSweepResult res;
    res.stage = stage;
    res.settings = s;

    res.error = checkPrerequisites(stage, s);
    if (!res.error.empty()) { return res; }

    done_.store(0);
    total_.store(s.points);
    activeProbe_.store(&probe);

    std::vector<float> power((size_t)s.points);
    char buf[160];
    for (int i = 0; i < s.points; ++i) {
        if (cancel_.load()) { res.cancelled = true; break; }
        double f = pointFreq(s, i);
        if (!probe.tune(f)) {
            std::snprintf(buf, sizeof(buf), "Tuning to %.6f MHz failed", f / 1e6);
            res.error = buf;
            if (!probe.lastError().empty()) { res.error += ": " + probe.lastError(); }
            break;
        }
        float db = 0.0f;
        if (!probe.measurePowerDb(s.settleSec, s.dwellSec, db)) {
            if (cancel_.load()) { res.cancelled = true; break; }
            std::snprintf(buf, sizeof(buf), "No power reading at %.6f MHz", f / 1e6);
            res.error = buf;
            if (!probe.lastError().empty()) { res.error += ": " + probe.lastError(); }
            break;
        }
        power[(size_t)i] = db;
        done_.store(i + 1);
    }

    activeProbe_.store(nullptr);
    probe.finish();

    if (res.cancelled) { res.error = "Cancelled"; return res; }
    if (!res.error.empty()) { return res; }

    res.finishedAt = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> l(mutex_);
    if (stage == SweepStage::Thru) {
        thruDb_ = power;
        thruSettings_ = s;
        thruTime_ = res.finishedAt;
        haveThru_ = true;
    }
    else if (stage == SweepStage::Noise) {
        noiseDb_ = power;
        noiseSettings_ = s;
        noiseTime_ = res.finishedAt;
        haveNoise_ = true;
    }
    else {
        bool useFloor = haveNoise_ && noiseSettings_.sameGrid(s);
        res.trace.reserve((size_t)s.points);
        for (int i = 0; i < s.points; ++i) {
            VNASweepPoint pt;
            pt.freqHz = pointFreq(s, i);
            pt.magnitudeDb = power[(size_t)i] - thruDb_[(size_t)i];
            pt.phaseDeg = NAN;
            if (useFloor) {
                pt.dynRangeDb = thruDb_[(size_t)i] - noiseDb_[(size_t)i];
                pt.valid = (power[(size_t)i] - noiseDb_[(size_t)i]) >= kUnreliableMarginDb;
            }
            res.trace.push_back(pt);
        }
    }
    res.ok = true;
    return res;
}

std::string ScalarSweep::start(SweepStage stage, const ScalarSweepSettings& s, std::unique_ptr<ScalarProbe> probe) {
    if (running_.load() || finished_.load() || thread_.joinable()) { return "A sweep is already running"; }
    if (!probe) { return "No receiver probe"; }
    std::string err = checkPrerequisites(stage, s);
    if (!err.empty()) { return err; }

    ownedProbe_ = std::move(probe);
    cancel_.store(false);
    done_.store(0);
    total_.store(s.points);
    running_.store(true);
    thread_ = std::thread([this, stage, s]() {
        asyncResult_ = runImpl(stage, s, *ownedProbe_);
        running_.store(false);
        finished_.store(true);
    });
    return "";
}

void ScalarSweep::cancel() {
    cancel_.store(true);
    ScalarProbe* p = activeProbe_.load();
    if (p) { p->requestCancel(); }
}

float ScalarSweep::progress() const {
    int t = total_.load();
    return t > 0 ? (float)done_.load() / (float)t : 0.0f;
}

bool ScalarSweep::pollFinished(ScalarSweepResult& out) {
    if (!finished_.load()) { return false; }
    if (thread_.joinable()) { thread_.join(); }
    out = std::move(asyncResult_);
    ownedProbe_.reset();
    finished_.store(false);
    return true;
}

} // namespace vna
} // namespace rf_tools
