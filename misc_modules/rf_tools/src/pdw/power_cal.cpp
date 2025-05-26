#include "power_cal.h"
#include <json.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <utils/flog.h>

namespace rf_tools {

PowerCalibrationTable::PowerCalibrationTable() {
    loadDefaultB210();
}

bool PowerCalibrationTable::loadFromJson(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(_mutex);
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            flog::warn("PowerCalibrationTable: could not open file {}", filePath);
            return false;
        }

        nlohmann::json root;
        file >> root;

        std::map<int, std::map<std::string, std::vector<CalPoint>>> newTables;

        for (auto& [chStr, portObj] : root.items()) {
            int ch = std::stoi(chStr);
            for (auto& [port, freqObj] : portObj.items()) {
                std::vector<CalPoint> points;
                for (auto& [freqStr, gainObj] : freqObj.items()) {
                    CalPoint pt;
                    pt.freq = std::stod(freqStr);
                    for (auto& [gainStr, refVal] : gainObj.items()) {
                        pt.gainToRefLevel[std::stof(gainStr)] = refVal.get<float>();
                    }
                    points.push_back(pt);
                }
                std::sort(points.begin(), points.end(), [](const CalPoint& a, const CalPoint& b) {
                    return a.freq < b.freq;
                });
                newTables[ch][port] = std::move(points);
            }
        }

        _tables = std::move(newTables);
        _name = filePath;
        _enabled = true;
        flog::info("PowerCalibrationTable: successfully loaded from {}", filePath);
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
    std::lock_guard<std::mutex> lock(_mutex);
    _tables.clear();
    _name = "USRP B210 Default";

    // Standard B210 empirical calibration grid (50 MHz to 6 GHz)
    // Gain vs 0 dBFS reference level (in dBm)
    static const double freqs[] = {
        50e6, 150e6, 250e6, 350e6, 450e6, 900e6, 1200e6, 1500e6, 2000e6, 2400e6, 3500e6, 5800e6
    };
    static const float gains[] = { 0.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f };

    for (int ch = 0; ch < 2; ++ch) {
        for (const std::string& port : { "RX2", "TX/RX" }) {
            std::vector<CalPoint> points;
            for (double f : freqs) {
                CalPoint pt;
                pt.freq = f;
                // Empirical curve: Reference level at 0 gain ~ +6 dBm, slopes -0.85 dBm per dB gain
                float freqFactor = static_cast<float>(std::log10(f / 1e6) * 1.5f);
                for (float g : gains) {
                    float refDbm = 6.0f - (g * 0.86f) + freqFactor;
                    pt.gainToRefLevel[g] = refDbm;
                }
                points.push_back(pt);
            }
            _tables[ch][port] = std::move(points);
        }
    }
}

float PowerCalibrationTable::getRefLevelDbm(double freqHz, float gainDb, const std::string& port, int channel) const {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_tables.empty()) {
        return 0.0f;
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

    float refLevel = getRefLevelDbm(pdw.absoluteFrequency, _gainDb, _port, 0);
    pdw.refLevelDbm = refLevel;
    pdw.peakPowerDbm = pdw.peakPower + refLevel;
    pdw.averagePowerDbm = pdw.averagePower + refLevel;
    pdw.noiseFloorDbm = pdw.noiseFloor + refLevel;
    pdw.isCalibratedDbm = true;
}

} // namespace rf_tools
