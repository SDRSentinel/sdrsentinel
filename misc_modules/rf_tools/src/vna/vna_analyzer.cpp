#include "vna_analyzer.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace rf_tools {
namespace vna {

void VNAAnalyzer::setConfig(const VNASweepConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

VNASweepConfig VNAAnalyzer::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void VNAAnalyzer::clearTrace() {
    std::lock_guard<std::mutex> lock(mutex_);
    trace_.clear();
    latestFit_ = ResonatorDipFit{};
    traceSource_.clear();
}

void VNAAnalyzer::setTraceSource(const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    traceSource_ = source;
}

std::string VNAAnalyzer::getTraceSource() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return traceSource_;
}

void VNAAnalyzer::addPoint(double freqHz, float magnitudeDb, float phaseDeg) {
    std::lock_guard<std::mutex> lock(mutex_);
    trace_.push_back({ freqHz, magnitudeDb, phaseDeg });
    if (trace_.size() >= 5) {
        latestFit_ = fitResonance(trace_, config_.findDips);
    }
}

void VNAAnalyzer::setFullTrace(const std::vector<VNASweepPoint>& trace) {
    std::lock_guard<std::mutex> lock(mutex_);
    trace_ = trace;
    latestFit_ = fitResonance(trace_, config_.findDips);
}

std::vector<VNASweepPoint> VNAAnalyzer::getTrace() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return trace_;
}

ResonatorDipFit VNAAnalyzer::getDipFit() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latestFit_;
}

ResonatorDipFit VNAAnalyzer::fitResonance(const std::vector<VNASweepPoint>& fullTrace, bool findDip) const {
    ResonatorDipFit fit;

    std::vector<VNASweepPoint> trace;
    trace.reserve(fullTrace.size());
    for (const auto& pt : fullTrace) {
        if (pt.valid && std::isfinite(pt.magnitudeDb)) { trace.push_back(pt); }
    }
    if (trace.size() < 7) return fit;

    // Estimate baseline from the outer 10% edges
    size_t edgeCount = std::max((size_t)2, trace.size() / 10);
    double edgeSum = 0.0;
    for (size_t i = 0; i < edgeCount; ++i) {
        edgeSum += trace[i].magnitudeDb + trace[trace.size() - 1 - i].magnitudeDb;
    }
    float baseline = static_cast<float>(edgeSum / (2 * edgeCount));
    fit.baselineDb = baseline;

    // Find extreme point (minimum for dip, maximum for peak)
    size_t extremumIdx = 0;
    float extremumVal = trace[0].magnitudeDb;

    for (size_t i = 1; i < trace.size(); ++i) {
        if (findDip) {
            if (trace[i].magnitudeDb < extremumVal) {
                extremumVal = trace[i].magnitudeDb;
                extremumIdx = i;
            }
        } else {
            if (trace[i].magnitudeDb > extremumVal) {
                extremumVal = trace[i].magnitudeDb;
                extremumIdx = i;
            }
        }
    }

    float depth = std::abs(baseline - extremumVal);
    if (depth < 1.5f) {
        // Not enough resonance depth
        return fit;
    }

    fit.resonantFreqHz = trace[extremumIdx].freqHz;
    fit.dipDepthDb = depth;

    // In a Lorentzian transmission notch S21(f) = 1 - (Q_L/Q_c)/(1 + 2j Q_L df/f0),
    // the loaded 3dB half-power bandwidth occurs where |S21|^2 drops halfway between baseline and minimum:
    // P_half = (P_baseline + P_min) / 2.
    // For a transmission peak (bandpass), the 3dB points are 3.01 dB below the peak.
    float targetDb;
    if (findDip) {
        double pBase = std::pow(10.0, baseline / 10.0);
        double pMin = std::pow(10.0, extremumVal / 10.0);
        double pHalf = 0.5 * (pBase + pMin);
        targetDb = static_cast<float>(10.0 * std::log10(pHalf));
    } else {
        targetDb = extremumVal - 3.0103f;
    }

    // Left search
    double fLeft = trace.front().freqHz;
    for (size_t i = extremumIdx; i > 0; --i) {
        bool crossed = findDip ? (trace[i].magnitudeDb >= targetDb) : (trace[i].magnitudeDb <= targetDb);
        if (crossed) {
            // Linear interpolate between i and i+1
            float db0 = trace[i].magnitudeDb, db1 = trace[i + 1].magnitudeDb;
            double f0 = trace[i].freqHz, f1 = trace[i + 1].freqHz;
            double alpha = (targetDb - db0) / (db1 - db0 + 1e-9f);
            fLeft = f0 + alpha * (f1 - f0);
            break;
        }
    }

    // Right search
    double fRight = trace.back().freqHz;
    for (size_t i = extremumIdx; i < trace.size() - 1; ++i) {
        bool crossed = findDip ? (trace[i].magnitudeDb >= targetDb) : (trace[i].magnitudeDb <= targetDb);
        if (crossed) {
            float db0 = trace[i - 1].magnitudeDb, db1 = trace[i].magnitudeDb;
            double f0 = trace[i - 1].freqHz, f1 = trace[i].freqHz;
            double alpha = (targetDb - db0) / (db1 - db0 + 1e-9f);
            fRight = f0 + alpha * (f1 - f0);
            break;
        }
    }

    double bw3db = std::abs(fRight - fLeft);
    if (bw3db > 1.0) {
        fit.bandwidth3dbHz = bw3db;
        fit.loadedQ = static_cast<float>(fit.resonantFreqHz / bw3db);

        if (findDip) {
            // Standard canonical notch resonator relationship:
            // 1/Q_L = 1/Q_i + 1/Q_c
            // |S21(f0)| = Q_L / Q_i = S21_min_lin
            // dipDepthDb = 20 * log10(Q_i / Q_L)
            float s21_min_lin = std::pow(10.0f, -depth / 20.0f);
            fit.internalQ = fit.loadedQ / std::max(1e-6f, s21_min_lin);
            fit.couplingQ = fit.loadedQ / std::max(1e-6f, 1.0f - s21_min_lin);
        } else {
            fit.internalQ = fit.loadedQ;
            fit.couplingQ = fit.loadedQ;
        }
        fit.valid = true;
    }

    return fit;
}

} // namespace vna
} // namespace rf_tools
