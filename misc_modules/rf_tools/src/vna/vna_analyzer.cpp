#include "vna_analyzer.h"
#include <cmath>
#include <complex>
#include <algorithm>
#include <numeric>

namespace rf_tools {
namespace vna {

VNAAnalyzer::VNAAnalyzer() {
    generateSyntheticResonatorSweep();
}

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

ResonatorDipFit VNAAnalyzer::fitResonance(const std::vector<VNASweepPoint>& trace, bool findDip) const {
    ResonatorDipFit fit;
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

    // Find 3 dB points relative to extremum
    float targetDb = findDip ? (extremumVal + 3.0f) : (extremumVal - 3.0f);

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
        // Internal Q estimate: Q_i = Q_L / (1 - 10^(-depth/20))
        float s21_min_lin = std::pow(10.0f, -depth / 20.0f);
        fit.internalQ = fit.loadedQ / std::max(1e-4f, 1.0f - s21_min_lin);
        fit.valid = true;
    }

    return fit;
}

void VNAAnalyzer::generateSyntheticResonatorSweep(double f0, double q, float depthDb) {
    std::lock_guard<std::mutex> lock(mutex_);
    trace_.clear();

    double bw = f0 / q;
    double span = bw * 8.0;
    config_.startFreqHz = f0 - span * 0.5;
    config_.stopFreqHz = f0 + span * 0.5;
    config_.numPoints = 201;

    double df = (config_.stopFreqHz - config_.startFreqHz) / (config_.numPoints - 1);
    float s21_min_lin = std::pow(10.0f, -depthDb / 20.0f);

    for (size_t i = 0; i < config_.numPoints; ++i) {
        double f = config_.startFreqHz + i * df;
        double deltaF = (f - f0) / (0.5 * bw);

        // Lorentzian S21 resonance response: S21(f) = 1 - (1 - S21_min) / (1 + j * 2 * Q * (f - f0) / f0)
        std::complex<double> denom(1.0, deltaF);
        std::complex<double> s21 = 1.0 - (1.0 - s21_min_lin) / denom;

        float magDb = static_cast<float>(20.0 * std::log10(std::abs(s21)));
        float phaseDeg = static_cast<float>(std::arg(s21) * 180.0 / M_PI);

        trace_.push_back({ f, magDb, phaseDeg });
    }

    latestFit_ = fitResonance(trace_, true);
}

} // namespace vna
} // namespace rf_tools
