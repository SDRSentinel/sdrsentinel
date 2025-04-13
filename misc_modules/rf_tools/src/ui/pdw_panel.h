#pragma once
#include "../pdw/pdw.h"
#include "../pdw/pulse_detector.h"
#include <deque>
#include <mutex>
#include <string>

namespace rf_tools {
    class PDWPanel {
    public:
        PDWPanel();
        ~PDWPanel();

        void addPDW(const PDW& pdw);
        void clear();

        void draw(PulseDetector& detector, double centerFreq, double vfoBw);

        size_t getTotalPulsesDetected() const { return _totalPulses; }

    private:
        void exportToCsv(const std::string& filepath);

        mutable std::mutex _mutex;
        std::deque<PDW> _recentPdws;
        static constexpr size_t MAX_STORED_PDWS = 200;

        uint64_t _totalPulses = 0;
        float _guiThreshold = -40.0f;
        float _guiMargin = 12.0f;
        float _guiMinPwUs = 0.5f;
        float _guiMaxPwUs = 500.0f;
        bool _autoScroll = true;
        char _exportPath[256] = "recordings/pdw_log.csv";
    };
}
