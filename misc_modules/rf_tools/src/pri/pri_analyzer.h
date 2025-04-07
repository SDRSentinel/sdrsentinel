#pragma once
#include "../pdw/pdw.h"
#include <vector>
#include <deque>
#include <mutex>
#include <string>

namespace rf_tools {
    enum class PRIModulation {
        UNKNOWN,
        CONSTANT,
        STAGGERED,
        JITTERED
    };

    struct PRIStats {
        PRIModulation modulation = PRIModulation::UNKNOWN;
        double meanPri = 0.0;          // seconds
        double minPri = 0.0;           // seconds
        double maxPri = 0.0;           // seconds
        double stdDevPri = 0.0;        // seconds
        double estimatedPrf = 0.0;     // Hz
        std::vector<double> staggerLevels; // Detected distinct PRI levels (seconds)
        size_t pulseCount = 0;
    };

    class PRIAnalyzer {
    public:
        PRIAnalyzer();
        ~PRIAnalyzer();

        void init(size_t historySize = 256);
        void addPulse(const PDW& pdw);
        void clear();

        PRIStats getStats() const;
        std::vector<double> getRecentPris() const;

        static const char* getModulationName(PRIModulation mod);

    private:
        void analyzeLocked();

        mutable std::mutex _mutex;
        size_t _historySize = 256;
        std::deque<double> _priHistory;
        PRIStats _cachedStats;
    };
}
