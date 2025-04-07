#include "pri_analyzer.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <map>

namespace rf_tools {
    PRIAnalyzer::PRIAnalyzer() {}
    PRIAnalyzer::~PRIAnalyzer() {}

    void PRIAnalyzer::init(size_t historySize) {
        std::lock_guard<std::mutex> lock(_mutex);
        _historySize = historySize;
        clear();
    }

    void PRIAnalyzer::clear() {
        std::lock_guard<std::mutex> lock(_mutex);
        _priHistory.clear();
        _cachedStats = PRIStats();
    }

    void PRIAnalyzer::addPulse(const PDW& pdw) {
        if (pdw.pri <= 1.0e-7 || pdw.pri > 1.0) {
            // Ignore invalid or too long PRI (e.g. > 1 second between pulses)
            return;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        _priHistory.push_back(pdw.pri);
        if (_priHistory.size() > _historySize) {
            _priHistory.pop_front();
        }

        analyzeLocked();
    }

    PRIStats PRIAnalyzer::getStats() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _cachedStats;
    }

    std::vector<double> PRIAnalyzer::getRecentPris() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return std::vector<double>(_priHistory.begin(), _priHistory.end());
    }

    const char* PRIAnalyzer::getModulationName(PRIModulation mod) {
        switch (mod) {
            case PRIModulation::CONSTANT:  return "Constant PRI (Fixed)";
            case PRIModulation::STAGGERED: return "Staggered PRI (Multi-rate)";
            case PRIModulation::JITTERED:  return "Jittered PRI (Spread)";
            case PRIModulation::UNKNOWN:
            default:                       return "Searching / Unknown";
        }
    }

    void PRIAnalyzer::analyzeLocked() {
        size_t n = _priHistory.size();
        _cachedStats.pulseCount = n;

        if (n < 6) {
            _cachedStats.modulation = PRIModulation::UNKNOWN;
            return;
        }

        // Basic stats
        double sum = 0.0;
        double minV = _priHistory[0];
        double maxV = _priHistory[0];

        for (double p : _priHistory) {
            sum += p;
            if (p < minV) minV = p;
            if (p > maxV) maxV = p;
        }

        double mean = sum / n;
        double sqDiffSum = 0.0;
        for (double p : _priHistory) {
            double d = p - mean;
            sqDiffSum += d * d;
        }
        double stdDev = std::sqrt(sqDiffSum / n);

        _cachedStats.meanPri = mean;
        _cachedStats.minPri = minV;
        _cachedStats.maxPri = maxV;
        _cachedStats.stdDevPri = stdDev;
        _cachedStats.estimatedPrf = (mean > 0.0) ? (1.0 / mean) : 0.0;
        _cachedStats.staggerLevels.clear();

        double cv = (mean > 0.0) ? (stdDev / mean) : 0.0;

        if (cv < 0.02) {
            // Very low variation: Constant PRI
            _cachedStats.modulation = PRIModulation::CONSTANT;
            _cachedStats.staggerLevels.push_back(mean);
        } else {
            // Cluster recent PRIs to detect multi-rate stagger
            // Tolerance: 3% of mean
            double tol = mean * 0.03;
            struct Cluster {
                double center = 0.0;
                size_t count = 0;
            };
            std::vector<Cluster> clusters;

            for (double p : _priHistory) {
                bool matched = false;
                for (auto& c : clusters) {
                    if (std::abs(p - c.center) <= tol) {
                        c.center = (c.center * c.count + p) / (c.count + 1);
                        c.count++;
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    clusters.push_back({ p, 1 });
                }
            }

            // Keep clusters with >= 15% of total pulses
            std::vector<double> validLevels;
            for (const auto& c : clusters) {
                if (static_cast<double>(c.count) / n >= 0.15) {
                    validLevels.push_back(c.center);
                }
            }

            std::sort(validLevels.begin(), validLevels.end());

            if (validLevels.size() >= 2 && validLevels.size() <= 8) {
                _cachedStats.modulation = PRIModulation::STAGGERED;
                _cachedStats.staggerLevels = validLevels;
            } else {
                _cachedStats.modulation = PRIModulation::JITTERED;
            }
        }
    }
}
