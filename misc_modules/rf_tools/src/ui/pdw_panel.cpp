#include "pdw_panel.h"
#include "../export/sigmf_exporter.h"
#include <imgui.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <fstream>
#include <iomanip>
#include <cstring>
#include <utils/flog.h>

namespace rf_tools {
    PDWPanel::PDWPanel() {}
    PDWPanel::~PDWPanel() {}

    void PDWPanel::addPDW(const PDW& pdw) {
        std::lock_guard<std::mutex> lock(_mutex);
        _totalPulses++;
        _recentPdws.push_back(pdw);
        if (_recentPdws.size() > MAX_STORED_PDWS) {
            _recentPdws.pop_front();
        }
    }

    void PDWPanel::clear() {
        std::lock_guard<std::mutex> lock(_mutex);
        _recentPdws.clear();
        _totalPulses = 0;
        _statusMsg[0] = '\0';
    }

    std::vector<PDW> PDWPanel::getRecentPdws() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return std::vector<PDW>(_recentPdws.begin(), _recentPdws.end());
    }

    void PDWPanel::draw(PulseDetector& detector, double centerFreq, double vfoBw) {
        float menuWidth = ImGui::GetContentRegionAvail().x;

        ImGui::Text("RF Status: CF = %.3f MHz | BW = %.3f MHz", centerFreq / 1.0e6, vfoBw / 1.0e6);
        ImGui::Separator();

        // Detection parameters
        bool autoThreshold = detector.getAutoThreshold();
        if (ImGui::Checkbox("Auto threshold##pdw_auto_thresh", &autoThreshold)) {
            detector.setAutoThreshold(autoThreshold);
        }

        if (autoThreshold) {
            _guiMargin = detector.getNoiseMargin();
            ImGui::LeftLabel("Above noise (dB)");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat("##pdw_margin", &_guiMargin, 3.0f, 30.0f, "%.1f dB")) {
                detector.setNoiseMargin(_guiMargin);
            }
            ImGui::Text("Noise: %.1f dBFS | Threshold: %.1f dBFS", detector.getNoiseFloorDb(), detector.getEffectiveThresholdDb());
        }
        else {
            _guiThreshold = detector.getThreshold();
            ImGui::LeftLabel("Threshold (dBFS)");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat("##pdw_thresh", &_guiThreshold, -100.0f, -5.0f, "%.1f dBFS")) {
                detector.setThreshold(_guiThreshold);
            }
        }

        _guiMinPwUs = static_cast<float>(detector.getMinPulseWidth() * 1.0e6);
        ImGui::LeftLabel("Min PW (us)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderFloat("##pdw_min_pw", &_guiMinPwUs, 0.1f, 50.0f, "%.2f us")) {
            detector.setPulseWidthLimits(_guiMinPwUs * 1.0e-6, _guiMaxPwUs * 1.0e-6);
        }

        _guiMaxPwUs = static_cast<float>(detector.getMaxPulseWidth() * 1.0e6);
        ImGui::LeftLabel("Max PW (us)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderFloat("##pdw_max_pw", &_guiMaxPwUs, 10.0f, 2000.0f, "%.1f us")) {
            detector.setPulseWidthLimits(_guiMinPwUs * 1.0e-6, _guiMaxPwUs * 1.0e-6);
        }

        ImGui::Spacing();
        ImGui::Text("Total Pulses: %llu | Buffer: %zu", (unsigned long long)_totalPulses, _recentPdws.size());

        if (ImGui::Button("Clear##pdw_clear")) {
            clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &_autoScroll);

        // Export and SigMF Section
        if (ImGui::CollapsingHeader("Export & SigMF Recording##pdw_export_hdr")) {
            const char* formats[] = { "CSV (.csv)", "JSON Session (.json)", "SigMF Metadata (.sigmf-meta)" };
            if (ImGui::Combo("Format##pdw_exp_fmt", &_exportFormatIdx, formats, IM_ARRAYSIZE(formats))) {
                if (_exportFormatIdx == 0) {
                    std::strncpy(_exportPath, "recordings/pdw_log.csv", sizeof(_exportPath));
                } else if (_exportFormatIdx == 1) {
                    std::strncpy(_exportPath, "recordings/pdw_session.json", sizeof(_exportPath));
                } else {
                    std::strncpy(_exportPath, "recordings/pdw_capture.sigmf-meta", sizeof(_exportPath));
                }
            }

            ImGui::InputText("Target File##pdw_exp_path", _exportPath, sizeof(_exportPath));

            if (ImGui::Button("Export Data Now##pdw_btn_export")) {
                auto pdws = getRecentPdws();
                bool ok = false;
                if (_exportFormatIdx == 0) {
                    ok = SigMFExporter::exportCSV(_exportPath, pdws);
                } else if (_exportFormatIdx == 1) {
                    ok = SigMFExporter::exportJSON(_exportPath, pdws, {}, 2000000.0, centerFreq);
                } else {
                    ok = SigMFExporter::exportSigMF(_exportPath, pdws, 2000000.0, centerFreq);
                }

                if (ok) {
                    std::snprintf(_statusMsg, sizeof(_statusMsg), "Exported %zu PDWs", pdws.size());
                } else {
                    std::snprintf(_statusMsg, sizeof(_statusMsg), "Export failed");
                }
            }

            if (_statusMsg[0] != '\0') {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "%s", _statusMsg);
            }
        }

        ImGui::Separator();

        if (_totalPulses == 0) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No pulses yet. Press play, tune to a pulsed signal and set the threshold just above the noise floor.");
            ImGui::Spacing();
        }

        // PDW table (9 columns: ID, TOA, PW, dF, Pk, Avg, SNR, PRI, DOA)
        int tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("##pdw_table", 9, tableFlags, ImVec2(0, 240))) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("TOA (s)", ImGuiTableColumnFlags_WidthFixed, 75.0f);
            ImGui::TableSetupColumn("PW (us)", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("dF (kHz)", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("Pk (dBFS)", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("Avg (dBFS)", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("SNR (dB)", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("PRI (us)", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("DOA (deg)", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableHeadersRow();

            std::lock_guard<std::mutex> lock(_mutex);
            for (const auto& p : _recentPdws) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%llu", (unsigned long long)p.pulseIndex);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.4f", p.toa);

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.2f", p.pulseWidth * 1.0e6);

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%+.1f", p.frequencyOffset / 1.0e3);

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.1f", p.peakPower);

                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%.1f", p.averagePower);

                ImGui::TableSetColumnIndex(6);
                ImGui::Text("%.1f", p.snr);

                ImGui::TableSetColumnIndex(7);
                if (p.pri > 0.0) {
                    ImGui::Text("%.1f", p.pri * 1.0e6);
                } else {
                    ImGui::Text("--");
                }

                ImGui::TableSetColumnIndex(8);
                if (p.hasDoa && p.doaAzimuth >= 0.0f) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%.1f", p.doaAzimuth);
                } else {
                    ImGui::TextDisabled("--");
                }
            }

            if (_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }

            ImGui::EndTable();
        }
    }
}
