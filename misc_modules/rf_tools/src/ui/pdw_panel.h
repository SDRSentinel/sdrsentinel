#pragma once
#include "../pdw/pdw.h"
#include "../pdw/pulse_detector.h"
#include <deque>
#include <vector>
#include <mutex>
#include <string>

namespace rf_tools {
    class PowerCalibrationTable;
    class PdwBinaryExporter;

    class PDWPanel {
    public:
        PDWPanel();
        ~PDWPanel();

        void addPDW(const PDW& pdw);
        void clear();

        void draw(PulseDetector& detector, double centerFreq, double vfoBw, PowerCalibrationTable* calTable = nullptr, PdwBinaryExporter* binExporter = nullptr);

        size_t getTotalPulsesDetected() const { return _totalPulses; }
        std::vector<PDW> getRecentPdws() const;

    private:
        mutable std::mutex _mutex;
        std::deque<PDW> _recentPdws;
        static constexpr size_t MAX_STORED_PDWS = 200;

        uint64_t _totalPulses = 0;
        float _guiThreshold = -40.0f;
        float _guiMargin = 12.0f;
        float _guiMinPwUs = 2.0f;
        float _guiMaxPwUs = 500.0f;
        bool _autoScroll = true;

        int _exportFormatIdx = 3; // 0: CSV, 1: JSON, 2: SigMF, 3: Binary/HDF5 (.pdwbin)
        char _exportPath[256] = "recordings/pdw_stream.pdwbin";
        char _customCalPath[256] = ""; // user-supplied table; the default GTRI table is embedded in the module
        char _statusMsg[128] = "";
        bool _displayDbm = true;
    };
}
