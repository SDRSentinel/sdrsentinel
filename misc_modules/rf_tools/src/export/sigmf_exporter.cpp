#include "sigmf_exporter.h"
#include <json.hpp>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <utils/flog.h>

namespace rf_tools {

std::string SigMFExporter::getIso8601Utc() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    gmtime_r(&tt, &tm);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

static void ensureDirectoryExists(const std::string& filePath) {
    try {
        std::filesystem::path p(filePath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
    } catch (...) {}
}

bool SigMFExporter::exportSigMF(const std::string& baseFilePath,
                               const std::vector<PDW>& pdws,
                               double sampleRate,
                               double centerFreqHz,
                               const std::string& author,
                               const std::string& description) {
    std::string cleanBase = baseFilePath;
    const std::string metaExt = ".sigmf-meta";
    const std::string dataExt = ".sigmf-data";

    if (cleanBase.size() >= metaExt.size() && cleanBase.rfind(metaExt) == cleanBase.size() - metaExt.size()) {
        cleanBase = cleanBase.substr(0, cleanBase.size() - metaExt.size());
    } else if (cleanBase.size() >= dataExt.size() && cleanBase.rfind(dataExt) == cleanBase.size() - dataExt.size()) {
        cleanBase = cleanBase.substr(0, cleanBase.size() - dataExt.size());
    }

    std::string metaPath = cleanBase + ".sigmf-meta";
    ensureDirectoryExists(metaPath);

    nlohmann::json root;

    // 1. Global metadata section
    root["global"] = {
        { "core:datatype", "cf32_le" },
        { "core:sample_rate", sampleRate },
        { "core:version", "1.0.0" },
        { "core:description", description },
        { "core:author", author },
        { "core:recorder", "SDR++ rf_tools" },
        { "core:hw", "SDR++ Multichannel Array / Passive DF Suite" }
    };

    // 2. Captures section
    nlohmann::json captures = nlohmann::json::array();
    captures.push_back({
        { "core:sample_start", 0 },
        { "core:frequency", centerFreqHz },
        { "core:datetime", getIso8601Utc() }
    });
    root["captures"] = captures;

    // 3. Annotations section (PDW pulses)
    nlohmann::json annotations = nlohmann::json::array();
    for (const auto& p : pdws) {
        uint64_t sampleCount = static_cast<uint64_t>(std::max(1.0, p.pulseWidth * sampleRate));
        nlohmann::json ann = {
            { "core:sample_start", p.sampleIndex },
            { "core:sample_count", sampleCount },
            { "core:generator", "rf_tools::PulseExtractor" },
            { "core:comment", "Pulse #" + std::to_string(p.pulseIndex) },
            { "rf:pulse_width_s", p.pulseWidth },
            { "rf:freq_offset_hz", p.frequencyOffset },
            { "rf:absolute_freq_hz", p.absoluteFrequency },
            { "rf:peak_power_dbfs", p.peakPower },
            { "rf:avg_power_dbfs", p.averagePower },
            { "rf:snr_db", p.snr },
            { "rf:noise_floor_dbfs", p.noiseFloor },
            { "rf:pri_s", p.pri },
            { "rf:has_doa", p.hasDoa }
        };

        if (p.hasDoa) {
            ann["rf:doa_azimuth_deg"] = p.doaAzimuth;
        }

        annotations.push_back(ann);
    }
    root["annotations"] = annotations;

    std::ofstream out(metaPath);
    if (!out.is_open()) {
        flog::error("Failed to open SigMF metadata file: {0}", metaPath);
        return false;
    }

    out << root.dump(4) << std::endl;
    flog::info("Exported SigMF metadata with {0} annotations to {1}", (uint64_t)pdws.size(), metaPath);
    return true;
}

bool SigMFExporter::exportJSON(const std::string& filePath,
                              const std::vector<PDW>& pdws,
                              const std::vector<TrackedEmitter>& emitters,
                              double sampleRate,
                              double centerFreqHz) {
    ensureDirectoryExists(filePath);

    nlohmann::json root;

    // Session metadata
    root["session"] = {
        { "timestamp", getIso8601Utc() },
        { "generator", "SDR++ rf_tools" },
        { "sample_rate_hz", sampleRate },
        { "center_frequency_hz", centerFreqHz },
        { "total_pulses", pdws.size() },
        { "total_emitters", emitters.size() }
    };

    // Tracked emitters
    nlohmann::json emitterArray = nlohmann::json::array();
    for (const auto& e : emitters) {
        nlohmann::json item = {
            { "id", e.id },
            { "active", e.active },
            { "carrier_frequency_hz", e.frequencyHz },
            { "pulse_width_s", e.pulseWidthSeconds },
            { "average_power_dbfs", e.averagePowerDbfs },
            { "pulse_count", e.pulseCount },
            { "first_seen_toa_s", e.firstSeenToa },
            { "last_seen_toa_s", e.lastSeenToa },
            { "has_doa", e.hasDoa }
        };

        if (e.hasDoa) {
            item["doa_azimuth_deg"] = e.doaAzimuthDeg;
        }

        if (e.priAnalyzer) {
            PRIStats stats = e.priAnalyzer->getStats();
            item["pri_analysis"] = {
                { "modulation", PRIAnalyzer::getModulationName(stats.modulation) },
                { "mean_pri_s", stats.meanPri },
                { "std_dev_pri_s", stats.stdDevPri },
                { "estimated_prf_hz", stats.estimatedPrf },
                { "stagger_levels_s", stats.staggerLevels }
            };
        }

        emitterArray.push_back(item);
    }
    root["emitters"] = emitterArray;

    // Pulse descriptor words
    nlohmann::json pdwArray = nlohmann::json::array();
    for (const auto& p : pdws) {
        nlohmann::json item = {
            { "pulse_index", p.pulseIndex },
            { "sample_index", p.sampleIndex },
            { "toa_s", p.toa },
            { "pulse_width_s", p.pulseWidth },
            { "freq_offset_hz", p.frequencyOffset },
            { "absolute_freq_hz", p.absoluteFrequency },
            { "peak_power_dbfs", p.peakPower },
            { "avg_power_dbfs", p.averagePower },
            { "snr_db", p.snr },
            { "noise_dbfs", p.noiseFloor },
            { "pri_s", p.pri },
            { "has_doa", p.hasDoa }
        };
        if (p.hasDoa) {
            item["doa_azimuth_deg"] = p.doaAzimuth;
        }
        pdwArray.push_back(item);
    }
    root["pdws"] = pdwArray;

    std::ofstream out(filePath);
    if (!out.is_open()) {
        flog::error("Failed to open JSON export file: {0}", filePath);
        return false;
    }

    out << root.dump(4) << std::endl;
    flog::info("Exported JSON session data ({0} PDWs, {1} Emitters) to {2}", (uint64_t)pdws.size(), (uint64_t)emitters.size(), filePath);
    return true;
}

bool SigMFExporter::exportCSV(const std::string& filePath,
                             const std::vector<PDW>& pdws) {
    ensureDirectoryExists(filePath);

    std::ofstream csv(filePath);
    if (!csv.is_open()) {
        flog::error("Could not open file for CSV export: {0}", filePath);
        return false;
    }

    csv << "PulseIndex,SampleIndex,TOA_s,PW_us,FreqOffset_Hz,AbsoluteFreq_Hz,PeakPwr_dBFS,AvgPwr_dBFS,SNR_dB,Noise_dBFS,PRI_us,DOA_deg\n";
    for (const auto& p : pdws) {
        csv << p.pulseIndex << ","
            << p.sampleIndex << ","
            << std::fixed << std::setprecision(6) << p.toa << ","
            << std::fixed << std::setprecision(3) << (p.pulseWidth * 1.0e6) << ","
            << std::fixed << std::setprecision(1) << p.frequencyOffset << ","
            << std::fixed << std::setprecision(1) << p.absoluteFrequency << ","
            << std::fixed << std::setprecision(2) << p.peakPower << ","
            << std::fixed << std::setprecision(2) << p.averagePower << ","
            << std::fixed << std::setprecision(2) << p.snr << ","
            << std::fixed << std::setprecision(2) << p.noiseFloor << ","
            << std::fixed << std::setprecision(3) << (p.pri * 1.0e6) << ",";
        if (p.hasDoa) {
            csv << std::fixed << std::setprecision(1) << p.doaAzimuth << "\n";
        } else {
            csv << "NaN\n";
        }
    }

    flog::info("Exported {0} PDW entries to CSV: {1}", (uint64_t)pdws.size(), filePath);
    return true;
}

} // namespace rf_tools
