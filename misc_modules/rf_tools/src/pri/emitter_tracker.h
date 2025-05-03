#pragma once
#include "../pdw/pdw.h"
#include "pri_analyzer.h"
#include <vector>
#include <deque>
#include <mutex>
#include <memory>
#include <string>

namespace rf_tools {

struct TrackedEmitter {
    uint32_t id = 0;
    double frequencyHz = 0.0;
    double pulseWidthSeconds = 0.0;
    float doaAzimuthDeg = -1.0f;
    bool hasDoa = false;
    float averagePowerDbfs = -120.0f;
    uint64_t pulseCount = 0;
    double firstSeenToa = 0.0;
    double lastSeenToa = 0.0;
    bool active = true;

    // Independent PRI analyzer for this specific emitter track
    std::shared_ptr<PRIAnalyzer> priAnalyzer;
};

class EmitterTracker {
public:
    EmitterTracker();
    ~EmitterTracker() = default;

    void init(double freqTolHz = 50.0e3, double pwTolUs = 1.5, float doaTolDeg = 15.0f, double timeoutSec = 5.0);
    void reset();

    // Process a newly extracted PDW and associate with an existing or new emitter track
    uint32_t processPDW(const PDW& pdw);

    // Get snapshot copy of all tracked emitters
    std::vector<TrackedEmitter> getEmitters() const;

    void setTolerances(double freqTolHz, double pwTolUs, float doaTolDeg);
    void setTimeout(double timeoutSec);

private:
    double calculateDistance(const PDW& pdw, const TrackedEmitter& emitter) const;
    void pruneInactive(double currentToa);

    double freqTolHz_ = 50.0e3;
    double pwTolSeconds_ = 1.5e-6;
    float doaTolDeg_ = 15.0f;
    double timeoutSec_ = 5.0;

    uint32_t nextEmitterId_ = 1;
    std::vector<TrackedEmitter> emitters_;
    mutable std::mutex mutex_;
};

} // namespace rf_tools
