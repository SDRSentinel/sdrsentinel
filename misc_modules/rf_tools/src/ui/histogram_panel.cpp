#include "histogram_panel.h"
#include <imgui.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <algorithm>
#include <cmath>

namespace rf_tools {
    HistogramPanel::HistogramPanel() {
        _priBins.resize(_binCount, 0.0f);
        _pwBins.resize(_binCount, 0.0f);
        _freqBins.resize(_binCount, 0.0f);
    }

    HistogramPanel::~HistogramPanel() {}

    void HistogramPanel::addPDW(const PDW& pdw) {
        std::lock_guard<std::mutex> lock(_mutex);
        _pdwHistory.push_back(pdw);
        if (_pdwHistory.size() > MAX_HISTORY) {
            _pdwHistory.pop_front();
        }
        updateBinsLocked();
    }

    void HistogramPanel::clear() {
        std::lock_guard<std::mutex> lock(_mutex);
        _pdwHistory.clear();
        std::fill(_priBins.begin(), _priBins.end(), 0.0f);
        std::fill(_pwBins.begin(), _pwBins.end(), 0.0f);
        std::fill(_freqBins.begin(), _freqBins.end(), 0.0f);
    }

    void HistogramPanel::updateBinsLocked() {
        if (_pdwHistory.empty()) return;

        if (_priBins.size() != (size_t)_binCount) {
            _priBins.resize(_binCount);
            _pwBins.resize(_binCount);
            _freqBins.resize(_binCount);
        }

        std::fill(_priBins.begin(), _priBins.end(), 0.0f);
        std::fill(_pwBins.begin(), _pwBins.end(), 0.0f);
        std::fill(_freqBins.begin(), _freqBins.end(), 0.0f);

        // Find min/max for dynamic scaling
        float minPri = 1e9f, maxPri = 0.0f;
        float minPw = 1e9f, maxPw = 0.0f;
        float minF = 1e9f, maxF = -1e9f;
        size_t priCount = 0;

        for (const auto& p : _pdwHistory) {
            float pwUs = static_cast<float>(p.pulseWidth * 1.0e6);
            if (pwUs < minPw) minPw = pwUs;
            if (pwUs > maxPw) maxPw = pwUs;

            float fKhz = static_cast<float>(p.frequencyOffset / 1.0e3);
            if (fKhz < minF) minF = fKhz;
            if (fKhz > maxF) maxF = fKhz;

            if (p.pri > 1.0e-7) {
                float priUs = static_cast<float>(p.pri * 1.0e6);
                if (priUs < minPri) minPri = priUs;
                if (priUs > maxPri) maxPri = priUs;
                priCount++;
            }
        }

        if (priCount > 0) {
            _priMinUs = std::max(0.0f, minPri * 0.9f);
            _priMaxUs = std::max(_priMinUs + 10.0f, maxPri * 1.1f);
        }
        _pwMinUs = std::max(0.0f, minPw * 0.9f);
        _pwMaxUs = std::max(_pwMinUs + 1.0f, maxPw * 1.1f);

        if (minF <= maxF) {
            _freqMinKhz = minF - 10.0f;
            _freqMaxKhz = maxF + 10.0f;
        }

        // Fill bins
        for (const auto& p : _pdwHistory) {
            // PW bin
            float pwUs = static_cast<float>(p.pulseWidth * 1.0e6);
            if (pwUs >= _pwMinUs && pwUs <= _pwMaxUs && _pwMaxUs > _pwMinUs) {
                int bin = static_cast<int>((pwUs - _pwMinUs) / (_pwMaxUs - _pwMinUs) * _binCount);
                if (bin >= _binCount) bin = _binCount - 1;
                if (bin >= 0) _pwBins[bin] += 1.0f;
            }

            // Frequency bin
            float fKhz = static_cast<float>(p.frequencyOffset / 1.0e3);
            if (fKhz >= _freqMinKhz && fKhz <= _freqMaxKhz && _freqMaxKhz > _freqMinKhz) {
                int bin = static_cast<int>((fKhz - _freqMinKhz) / (_freqMaxKhz - _freqMinKhz) * _binCount);
                if (bin >= _binCount) bin = _binCount - 1;
                if (bin >= 0) _freqBins[bin] += 1.0f;
            }

            // PRI bin
            if (p.pri > 1.0e-7) {
                float priUs = static_cast<float>(p.pri * 1.0e6);
                if (priUs >= _priMinUs && priUs <= _priMaxUs && _priMaxUs > _priMinUs) {
                    int bin = static_cast<int>((priUs - _priMinUs) / (_priMaxUs - _priMinUs) * _binCount);
                    if (bin >= _binCount) bin = _binCount - 1;
                    if (bin >= 0) _priBins[bin] += 1.0f;
                }
            }
        }
    }

    void HistogramPanel::draw(const PRIAnalyzer& priAnalyzer) {
        float menuWidth = ImGui::GetContentRegionAvail().x;
        PRIStats stats = priAnalyzer.getStats();

        // 1. Radar PRI Classification Header
        ImGui::Text("Radar Sinyal Modülasyon Sınıflandırması:");
        ImVec4 modColor(0.8f, 0.8f, 0.8f, 1.0f);
        if (stats.modulation == PRIModulation::CONSTANT) {
            modColor = ImVec4(0.2f, 1.0f, 0.3f, 1.0f); // Green
        } else if (stats.modulation == PRIModulation::STAGGERED) {
            modColor = ImVec4(0.2f, 0.8f, 1.0f, 1.0f); // Cyan
        } else if (stats.modulation == PRIModulation::JITTERED) {
            modColor = ImVec4(1.0f, 0.7f, 0.2f, 1.0f); // Orange
        }

        ImGui::TextColored(modColor, "Tip: %s", PRIAnalyzer::getModulationName(stats.modulation));

        if (stats.pulseCount >= 6) {
            ImGui::Text("Ort. PRI: %.2f us | StdDev: %.2f us | PRF: %.1f Hz",
                stats.meanPri * 1.0e6, stats.stdDevPri * 1.0e6, stats.estimatedPrf);

            if (stats.modulation == PRIModulation::STAGGERED && !stats.staggerLevels.empty()) {
                std::string staggerStr = "Kademeler: ";
                for (size_t i = 0; i < stats.staggerLevels.size(); ++i) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "[%zu] %.1fus  ", i + 1, stats.staggerLevels[i] * 1.0e6);
                    staggerStr += buf;
                }
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f), "%s", staggerStr.c_str());
            }
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Yeterli darbe toplanıyor (%zu/6)...", stats.pulseCount);
        }

        ImGui::Separator();

        // Controls
        ImGui::LeftLabel("Bin Sayısı");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderInt("##hist_bins", &_binCount, 16, 64)) {
            std::lock_guard<std::mutex> lock(_mutex);
            updateBinsLocked();
        }

        if (ImGui::Button("Sıfırla##hist_clear")) {
            clear();
        }
        ImGui::SameLine();
        ImGui::Text("Örnek Sayısı: %zu", _pdwHistory.size());

        ImGui::Spacing();

        // 2. PRI Histogram Plot
        char priOverlay[64];
        snprintf(priOverlay, sizeof(priOverlay), "PRI: [%.1f - %.1f us]", _priMinUs, _priMaxUs);
        ImGui::TextUnformatted("PRI Dağılımı:");
        {
            std::lock_guard<std::mutex> lock(_mutex);
            ImGui::PlotHistogram("##pri_hist", _priBins.data(), (int)_priBins.size(), 0, priOverlay, 0.0f, FLT_MAX, ImVec2(menuWidth, 80));
        }

        // 3. Pulse Width (PW) Histogram Plot
        char pwOverlay[64];
        snprintf(pwOverlay, sizeof(pwOverlay), "PW: [%.2f - %.2f us]", _pwMinUs, _pwMaxUs);
        ImGui::TextUnformatted("Darbe Genişliği (PW) Dağılımı:");
        {
            std::lock_guard<std::mutex> lock(_mutex);
            ImGui::PlotHistogram("##pw_hist", _pwBins.data(), (int)_pwBins.size(), 0, pwOverlay, 0.0f, FLT_MAX, ImVec2(menuWidth, 80));
        }

        // 4. Frequency Offset Distribution Plot
        char freqOverlay[64];
        snprintf(freqOverlay, sizeof(freqOverlay), "dF: [%.1f - %.1f kHz]", _freqMinKhz, _freqMaxKhz);
        ImGui::TextUnformatted("Frekans Sapması (dF) Dağılımı:");
        {
            std::lock_guard<std::mutex> lock(_mutex);
            ImGui::PlotHistogram("##freq_hist", _freqBins.data(), (int)_freqBins.size(), 0, freqOverlay, 0.0f, FLT_MAX, ImVec2(menuWidth, 80));
        }
    }
}
