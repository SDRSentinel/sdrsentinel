#pragma once
#include "../pdw/pdw.h"
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace rf_tools {

#pragma pack(push, 1)
struct PdwBinaryRecord {
    uint64_t pulseIndex;
    uint64_t sampleIndex;
    int64_t courseToa;
    uint64_t fineToa;
    double toa;
    double pulseWidth;
    uint32_t pulseWidthSamples;
    double frequencyOffset;
    double absoluteFrequency;
    float peakPower;       // dBFS or calibrated dBm
    float averagePower;    // dBFS or calibrated dBm
    float noiseFloor;      // dBFS or calibrated dBm
    float snr;
    float phase;
    double pri;
    float doaAzimuth;
    uint8_t hasDoa;
    uint8_t isCalibrated;
};

struct PdwBinaryHeader {
    char magic[8];         // "SDRPPPDW"
    uint32_t version;      // 2 (1 files lack timeBaseUnix)
    uint32_t headerSize;   // sizeof(PdwBinaryHeader)
    uint32_t recordSize;   // sizeof(PdwBinaryRecord)
    double sampleRate;
    double centerFreqHz;
    float refLevelDbm;
    uint8_t isCalibrated;
    int64_t startTimeUnix; // wall-clock second at which recording started
    double timeBaseUnix;   // wall-clock Unix time of detector sample 0 (v2), absolute TOA = timeBaseUnix + toa
    char reserved[24];
};
#pragma pack(pop)

class PdwBinaryExporter {
public:
    PdwBinaryExporter();
    ~PdwBinaryExporter();

    bool startRecording(const std::string& filePath, double sampleRate, double centerFreqHz, float refLevelDbm = 0.0f, bool isCalibrated = false, double timeBaseUnix = 0.0);
    void writePulse(const PDW& pdw);
    void writePulses(const std::vector<PDW>& pdws);
    void stopRecording();

    bool isRecording() const { return _isRecording.load(); }
    uint64_t getRecordCount() const { return _recordCount.load(); }
    std::string getCurrentFilePath() const;

private:
    std::atomic<bool> _isRecording{ false };
    std::atomic<uint64_t> _recordCount{ 0 };
    std::string _filePath;
    std::ofstream _file;
    mutable std::mutex _mutex;
};

} // namespace rf_tools
