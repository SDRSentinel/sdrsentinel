#include "emitter_tracker.h"
#include <cmath>
#include <algorithm>

namespace rf_tools {

EmitterTracker::EmitterTracker() {
    init(50.0e3, 1.5, 15.0f, 5.0);
}

void EmitterTracker::init(double freqTolHz, double pwTolUs, float doaTolDeg, double timeoutSec) {
    std::lock_guard<std::mutex> lock(mutex_);
    freqTolHz_ = std::max(freqTolHz, 1.0e3);
    pwTolSeconds_ = std::max(pwTolUs, 0.1) * 1.0e-6;
    doaTolDeg_ = std::max(doaTolDeg, 1.0f);
    timeoutSec_ = std::max(timeoutSec, 0.5);
    reset();
}

void EmitterTracker::reset() {
    emitters_.clear();
    nextEmitterId_ = 1;
}

double EmitterTracker::calculateDistance(const PDW& pdw, const TrackedEmitter& emitter) const {
    double df = (pdw.absoluteFrequency - emitter.frequencyHz) / freqTolHz_;
    double dpw = (pdw.pulseWidth - emitter.pulseWidthSeconds) / pwTolSeconds_;

    double ddoa = 0.0;
    if (pdw.hasDoa && emitter.hasDoa) {
        ddoa = static_cast<double>(pdw.doaAzimuth - emitter.doaAzimuthDeg) / doaTolDeg_;
    }

    return (df * df + dpw * dpw + ddoa * ddoa);
}

void EmitterTracker::pruneInactive(double currentToa) {
    for (auto& e : emitters_) {
        if (e.active && (currentToa - e.lastSeenToa > timeoutSec_)) {
            e.active = false;
        }
    }
}

uint32_t EmitterTracker::processPDW(const PDW& pdw) {
    std::lock_guard<std::mutex> lock(mutex_);
    pruneInactive(pdw.toa);

    double min_dist = 1.0; // Distance threshold for association
    int best_match_idx = -1;

    for (size_t i = 0; i < emitters_.size(); ++i) {
        if (!emitters_[i].active) continue;

        double d = calculateDistance(pdw, emitters_[i]);
        if (d < min_dist) {
            min_dist = d;
            best_match_idx = static_cast<int>(i);
        }
    }

    if (best_match_idx >= 0) {
        // Associate with existing emitter track
        auto& e = emitters_[best_match_idx];
        e.pulseCount++;
        e.lastSeenToa = pdw.toa;
        e.active = true;

        // Exponential moving average updates for continuous tracking
        e.frequencyHz = 0.85 * e.frequencyHz + 0.15 * pdw.absoluteFrequency;
        e.pulseWidthSeconds = 0.85 * e.pulseWidthSeconds + 0.15 * pdw.pulseWidth;
        e.averagePowerDbfs = 0.85f * e.averagePowerDbfs + 0.15f * pdw.averagePower;

        if (pdw.hasDoa) {
            if (e.hasDoa) {
                e.doaAzimuthDeg = 0.85f * e.doaAzimuthDeg + 0.15f * pdw.doaAzimuth;
            } else {
                e.doaAzimuthDeg = pdw.doaAzimuth;
                e.hasDoa = true;
            }
        }

        e.priAnalyzer->addPulse(pdw);
        return e.id;
    }

    // Create a new emitter track
    TrackedEmitter new_emitter;
    new_emitter.id = nextEmitterId_++;
    new_emitter.frequencyHz = pdw.absoluteFrequency;
    new_emitter.pulseWidthSeconds = pdw.pulseWidth;
    new_emitter.doaAzimuthDeg = pdw.hasDoa ? pdw.doaAzimuth : -1.0f;
    new_emitter.hasDoa = pdw.hasDoa;
    new_emitter.averagePowerDbfs = pdw.averagePower;
    new_emitter.pulseCount = 1;
    new_emitter.firstSeenToa = pdw.toa;
    new_emitter.lastSeenToa = pdw.toa;
    new_emitter.active = true;

    new_emitter.priAnalyzer = std::make_shared<PRIAnalyzer>();
    new_emitter.priAnalyzer->init(256);
    new_emitter.priAnalyzer->addPulse(pdw);

    emitters_.push_back(new_emitter);

    // Limit maximum tracked emitters to prevent memory leak
    if (emitters_.size() > 64) {
        emitters_.erase(emitters_.begin());
    }

    return new_emitter.id;
}

std::vector<TrackedEmitter> EmitterTracker::getEmitters() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return emitters_;
}

void EmitterTracker::setTolerances(double freqTolHz, double pwTolUs, float doaTolDeg) {
    std::lock_guard<std::mutex> lock(mutex_);
    freqTolHz_ = std::max(freqTolHz, 1.0e3);
    pwTolSeconds_ = std::max(pwTolUs, 0.1) * 1.0e-6;
    doaTolDeg_ = std::max(doaTolDeg, 1.0f);
}

void EmitterTracker::setTimeout(double timeoutSec) {
    std::lock_guard<std::mutex> lock(mutex_);
    timeoutSec_ = std::max(timeoutSec, 0.5);
}

} // namespace rf_tools
