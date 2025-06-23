#pragma once
#include "pdw.h"
#include <string>
#include <map>
#include <vector>
#include <mutex>

namespace rf_tools {

class PowerCalibrationTable {
public:
    PowerCalibrationTable();
    ~PowerCalibrationTable() = default;

    // Load calibration table from JSON file
    bool loadFromJson(const std::string& filePath);

    // Save current calibration table to JSON file
    bool saveToJson(const std::string& filePath) const;

    // Load built-in USRP B210 calibration curve
    void loadDefaultB210();

    // Enable / disable calibration
    void setEnabled(bool enabled) { _enabled = enabled; }
    bool isEnabled() const { return _enabled; }

    // Set hardware gain in dB (e.g. 0 to 80 dB)
    void setHardwareGain(float gainDb) { _gainDb = gainDb; }
    float getHardwareGain() const { return _gainDb; }

    // Set selected port (e.g., "RX2" or "TX/RX")
    void setPort(const std::string& port) { _port = port; }
    const std::string& getPort() const { return _port; }

    // Interpolate 0 dBFS reference level in dBm for given frequency and gain
    float getRefLevelDbm(double freqHz, float gainDb, const std::string& port = "RX2", int channel = 0) const;

    // Calibrate power in dBFS to dBm
    float toCalibratedDbm(float powerDbfs, double freqHz, float gainDb, const std::string& port = "RX2", int channel = 0) const;

    // Apply calibration in-place to a PDW
    void calibrate(PDW& pdw) const;

    // Official hardware frequency boundaries for USRP B210 (Analog Devices AD9361 RFIC: 70 MHz to 6.0 GHz).
    // Frequencies below 70 MHz (e.g. 50-70 MHz) exist in empirical GTRI calibration data but are outside official AD9361 specifications.
    static constexpr double B210_MIN_OFFICIAL_FREQ_HZ = 70.0e6;
    static constexpr double B210_MAX_OFFICIAL_FREQ_HZ = 6.0e9;

    bool isFrequencyInOfficialHardwareRange(double freqHz) const {
        return freqHz >= B210_MIN_OFFICIAL_FREQ_HZ && freqHz <= B210_MAX_OFFICIAL_FREQ_HZ;
    }

    // Status / info
    bool isLoaded() const { return !_tables.empty(); }
    std::string getCalibrationName() const { return _name; }

private:
    struct CalPoint {
        double freq;
        std::map<float, float> gainToRefLevel; // Gain (dB) -> Ref Level (dBm)
    };

    mutable bool _warnedOutOfRange = false; // re-armed whenever a new table is loaded
    bool _enabled = false;
    float _gainDb = 40.0f;
    std::string _port = "RX2";
    std::string _name = "None";

    mutable std::mutex _mutex;
    // Map: channel -> port -> list of CalPoints sorted by freq
    std::map<int, std::map<std::string, std::vector<CalPoint>>> _tables;
};

} // namespace rf_tools
