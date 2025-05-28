#include "pdw_binary_exporter.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <utils/flog.h>

namespace rf_tools {

PdwBinaryExporter::PdwBinaryExporter() {}

PdwBinaryExporter::~PdwBinaryExporter() {
    stopRecording();
}

bool PdwBinaryExporter::startRecording(const std::string& filePath, double sampleRate, double centerFreqHz, float refLevelDbm, bool isCalibrated) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_isRecording.load()) {
        stopRecording();
    }

    try {
        std::filesystem::path p(filePath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
    } catch (...) {}

    _file.open(filePath, std::ios::binary | std::ios::trunc);
    if (!_file.is_open()) {
        flog::error("PdwBinaryExporter: Failed to open file: {}", filePath);
        return false;
    }

    PdwBinaryHeader header;
    std::memset(&header, 0, sizeof(header));
    std::memcpy(header.magic, "SDRPPPDW", 8);
    header.version = 1;
    header.headerSize = sizeof(PdwBinaryHeader);
    header.recordSize = sizeof(PdwBinaryRecord);
    header.sampleRate = sampleRate;
    header.centerFreqHz = centerFreqHz;
    header.refLevelDbm = refLevelDbm;
    header.isCalibrated = isCalibrated ? 1 : 0;
    header.startTimeUnix = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    _file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!_file.good()) {
        _file.close();
        return false;
    }

    _filePath = filePath;
    _recordCount.store(0);
    _isRecording.store(true);
    flog::info("PdwBinaryExporter: Started recording to {}", filePath);
    return true;
}

void PdwBinaryExporter::writePulse(const PDW& pdw) {
    if (!_isRecording.load()) return;
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_file.is_open()) return;

    PdwBinaryRecord rec;
    rec.pulseIndex = pdw.pulseIndex;
    rec.sampleIndex = pdw.sampleIndex;
    rec.courseToa = pdw.courseToa;
    rec.fineToa = pdw.fineToa;
    rec.toa = pdw.toa;
    rec.pulseWidth = pdw.pulseWidth;
    rec.pulseWidthSamples = pdw.pulseWidthSamples;
    rec.frequencyOffset = pdw.frequencyOffset;
    rec.absoluteFrequency = pdw.absoluteFrequency;
    rec.peakPower = pdw.isCalibratedDbm ? pdw.peakPowerDbm : pdw.peakPower;
    rec.averagePower = pdw.isCalibratedDbm ? pdw.averagePowerDbm : pdw.averagePower;
    rec.noiseFloor = pdw.isCalibratedDbm ? pdw.noiseFloorDbm : pdw.noiseFloor;
    rec.snr = pdw.snr;
    rec.phase = pdw.phase;
    rec.pri = pdw.pri;
    rec.doaAzimuth = pdw.doaAzimuth;
    rec.hasDoa = pdw.hasDoa ? 1 : 0;
    rec.isCalibrated = pdw.isCalibratedDbm ? 1 : 0;

    _file.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
    _recordCount.fetch_add(1, std::memory_order_relaxed);
}

void PdwBinaryExporter::writePulses(const std::vector<PDW>& pdws) {
    if (!_isRecording.load() || pdws.empty()) return;
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_file.is_open()) return;

    for (const auto& pdw : pdws) {
        PdwBinaryRecord rec;
        rec.pulseIndex = pdw.pulseIndex;
        rec.sampleIndex = pdw.sampleIndex;
        rec.courseToa = pdw.courseToa;
        rec.fineToa = pdw.fineToa;
        rec.toa = pdw.toa;
        rec.pulseWidth = pdw.pulseWidth;
        rec.pulseWidthSamples = pdw.pulseWidthSamples;
        rec.frequencyOffset = pdw.frequencyOffset;
        rec.absoluteFrequency = pdw.absoluteFrequency;
        rec.peakPower = pdw.isCalibratedDbm ? pdw.peakPowerDbm : pdw.peakPower;
        rec.averagePower = pdw.isCalibratedDbm ? pdw.averagePowerDbm : pdw.averagePower;
        rec.noiseFloor = pdw.isCalibratedDbm ? pdw.noiseFloorDbm : pdw.noiseFloor;
        rec.snr = pdw.snr;
        rec.phase = pdw.phase;
        rec.pri = pdw.pri;
        rec.doaAzimuth = pdw.doaAzimuth;
        rec.hasDoa = pdw.hasDoa ? 1 : 0;
        rec.isCalibrated = pdw.isCalibratedDbm ? 1 : 0;

        _file.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
    }
    _recordCount.fetch_add(pdws.size(), std::memory_order_relaxed);
}

void PdwBinaryExporter::stopRecording() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_isRecording.load()) {
        if (_file.is_open()) {
            _file.flush();
            _file.close();
        }
        _isRecording.store(false);
        flog::info("PdwBinaryExporter: Stopped recording. Total pulses written: {0}", std::to_string(_recordCount.load()));
    }
}

std::string PdwBinaryExporter::getCurrentFilePath() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _filePath;
}

} // namespace rf_tools
