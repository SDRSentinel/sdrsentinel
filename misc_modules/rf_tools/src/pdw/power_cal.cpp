#include "power_cal.h"
#include <json.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <utils/flog.h>

namespace rf_tools {

// Generated at build time from b210_cal.json (see CMakeLists.txt)
extern const char* const kB210CalChunks[];
extern const unsigned kB210CalChunkCount;

PowerCalibrationTable::PowerCalibrationTable() {
    loadDefaultB210();
}

bool PowerCalibrationTable::loadFromJson(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        flog::warn("PowerCalibrationTable: could not open file {}", filePath);
        return false;
    }
    std::stringstream buf;
    buf << file.rdbuf();
    return parseTables(buf.str(), filePath, true);
}

bool PowerCalibrationTable::parseTables(const std::string& text, const std::string& name, bool enable) {
    std::lock_guard<std::mutex> lock(_mutex);
    try {
        nlohmann::json root = nlohmann::json::parse(text);

        std::map<int, std::map<std::string, std::vector<CalPoint>>> newTables;

        for (auto& [chStr, portObj] : root.items()) {
            int ch = std::stoi(chStr);
            for (auto& [port, freqObj] : portObj.items()) {
                std::vector<CalPoint> points;
                for (auto& [freqStr, gainObj] : freqObj.items()) {
                    CalPoint pt;
                    pt.freq = std::stod(freqStr);
                    for (auto& [gainStr, refVal] : gainObj.items()) {
                        // Missing measurements are stored as null; skip them so interpolation never sees NaN
                        if (!refVal.is_number()) continue;
                        float ref = refVal.get<float>();
                        if (!std::isfinite(ref)) continue;
                        pt.gainToRefLevel[std::stof(gainStr)] = ref;
                    }
                    if (pt.gainToRefLevel.empty()) continue;
                    points.push_back(pt);
                }
                std::sort(points.begin(), points.end(), [](const CalPoint& a, const CalPoint& b) {
                    return a.freq < b.freq;
                });
                if (!points.empty()) newTables[ch][port] = std::move(points);
            }
        }

        if (newTables.empty()) {
            flog::error("PowerCalibrationTable: {} contains no calibration points", name);
            return false;
        }

        _tables = std::move(newTables);
        _name = name;
        _warnedOutOfRange = false;
        if (enable) _enabled = true;
        flog::info("PowerCalibrationTable: successfully loaded from {}", name);
        return true;
    } catch (const std::exception& e) {
        flog::error("PowerCalibrationTable: error parsing JSON: {}", e.what());
        return false;
    }
}

bool PowerCalibrationTable::saveToJson(const std::string& filePath) const {
    std::lock_guard<std::mutex> lock(_mutex);
    try {
        nlohmann::json root;
        for (const auto& [ch, portMap] : _tables) {
            for (const auto& [port, points] : portMap) {
                for (const auto& pt : points) {
                    std::string freqKey = std::to_string(static_cast<uint64_t>(pt.freq));
                    for (const auto& [g, ref] : pt.gainToRefLevel) {
                        std::string gainKey = std::to_string(static_cast<int>(g));
                        root[std::to_string(ch)][port][freqKey][gainKey] = ref;
                    }
                }
            }
        }
        std::ofstream out(filePath);
        if (!out.is_open()) return false;
        out << root.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

void PowerCalibrationTable::loadDefaultB210() {
    // Default table: USRP B210 reference levels measured by GTRI, taken from b210_pdw.pickle of gr-pdw
    // (https://github.com/gtri/gr-pdw, GPLv3, Copyright 2024 GTRI) and converted to src/pdw/b210_cal.json
    // (channel -> port -> frequency in Hz -> gain in dB -> 0 dBFS reference level in dBm; null = not measured).
    // The JSON is embedded at build time (see CMakeLists.txt) so it does not depend on the working directory.
    std::string text;
    for (unsigned i = 0; i < kB210CalChunkCount; ++i) {
        text += kB210CalChunks[i];
    }
    if (!parseTables(text, DEFAULT_B210_NAME, false)) {
        flog::error("PowerCalibrationTable: could not load the embedded GTRI B210 table, power calibration disabled");
        std::lock_guard<std::mutex> lock(_mutex);
        _tables.clear();
        _name = "None";
        _enabled = false;
    }
}

float PowerCalibrationTable::getRefLevelDbm(double freqHz, float gainDb, const std::string& port, int channel) const {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_tables.empty()) {
        return 0.0f;
    }

    if (!isFrequencyInOfficialHardwareRange(freqHz) && !_warnedOutOfRange) {
        _warnedOutOfRange = true;
        flog::warn("PowerCalibrationTable: Frequency {0} MHz is outside official USRP B210 (AD9361) range [70 MHz - 6.0 GHz]; calibration values in 50-70 MHz are empirical GTRI uncertified.", freqHz / 1e6);
    }

    auto chIt = _tables.find(channel);
    if (chIt == _tables.end()) chIt = _tables.begin();

    auto portIt = chIt->second.find(port);
    if (portIt == chIt->second.end()) portIt = chIt->second.begin();

    const auto& points = portIt->second;
    if (points.empty()) return 0.0f;

    // Helper: interpolate gain at a given CalPoint
    auto interpGain = [](const CalPoint& pt, float g) -> float {
        if (pt.gainToRefLevel.empty()) return 0.0f;
        auto it = pt.gainToRefLevel.lower_bound(g);
        if (it == pt.gainToRefLevel.begin()) return it->second;
        if (it == pt.gainToRefLevel.end()) return std::prev(it)->second;
        auto prevIt = std::prev(it);
        float g0 = prevIt->first, g1 = it->first;
        float r0 = prevIt->second, r1 = it->second;
        float alpha = (g - g0) / (g1 - g0 + 1e-9f);
        return r0 + alpha * (r1 - r0);
    };

    // Frequency interpolation
    if (freqHz <= points.front().freq) {
        return interpGain(points.front(), gainDb);
    }
    if (freqHz >= points.back().freq) {
        return interpGain(points.back(), gainDb);
    }

    auto fIt = std::lower_bound(points.begin(), points.end(), freqHz, [](const CalPoint& pt, double f) {
        return pt.freq < f;
    });

    auto prevFIt = std::prev(fIt);
    float ref0 = interpGain(*prevFIt, gainDb);
    float ref1 = interpGain(*fIt, gainDb);
    double fAlpha = (freqHz - prevFIt->freq) / (fIt->freq - prevFIt->freq + 1e-9);
    return static_cast<float>(ref0 + fAlpha * (ref1 - ref0));
}

float PowerCalibrationTable::toCalibratedDbm(float powerDbfs, double freqHz, float gainDb, const std::string& port, int channel) const {
    if (!_enabled) return powerDbfs;
    float refLevel = getRefLevelDbm(freqHz, gainDb, port, channel);
    return powerDbfs + refLevel;
}

void PowerCalibrationTable::calibrate(PDW& pdw) const {
    if (!_enabled) {
        pdw.isCalibratedDbm = false;
        pdw.peakPowerDbm = pdw.peakPower;
        pdw.averagePowerDbm = pdw.averagePower;
        pdw.noiseFloorDbm = pdw.noiseFloor;
        pdw.refLevelDbm = 0.0f;
        return;
    }

    float refLevel = getRefLevelDbm(pdw.absoluteFrequency, _gainDb, _port, _channel);
    pdw.refLevelDbm = refLevel;
    pdw.peakPowerDbm = pdw.peakPower + refLevel;
    pdw.averagePowerDbm = pdw.averagePower + refLevel;
    pdw.noiseFloorDbm = pdw.noiseFloor + refLevel;
    pdw.isCalibratedDbm = true;
}

} // namespace rf_tools
