#include "pdw_panel.h"
#include <imgui.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <fstream>
#include <iomanip>
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
    }

    void PDWPanel::exportToCsv(const std::string& filepath) {
        std::lock_guard<std::mutex> lock(_mutex);
        std::ofstream csv(filepath);
        if (!csv.is_open()) {
            flog::error("Could not open file for PDW CSV export: {0}", filepath);
            return;
        }

        csv << "PulseIndex,SampleIndex,TOA_s,PW_us,FreqOffset_Hz,AbsoluteFreq_Hz,PeakPwr_dBFS,AvgPwr_dBFS,SNR_dB,Noise_dBFS,PRI_us\n";
        for (const auto& p : _recentPdws) {
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
                << std::fixed << std::setprecision(3) << (p.pri * 1.0e6) << "\n";
        }
        flog::info("Exported {0} PDW entries to {1}", (uint64_t)_recentPdws.size(), filepath);
    }

    void PDWPanel::draw(PulseDetector& detector, double centerFreq, double vfoBw) {
        float menuWidth = ImGui::GetContentRegionAvail().x;

        ImGui::Text("RF Status: CF = %.3f MHz | BW = %.3f MHz", centerFreq / 1.0e6, vfoBw / 1.0e6);
        ImGui::Separator();

        // Detection parameters
        _guiThreshold = detector.getThreshold();
        ImGui::LeftLabel("Threshold (dBFS)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderFloat("##pdw_thresh", &_guiThreshold, -100.0f, -5.0f, "%.1f dBFS")) {
            detector.setThreshold(_guiThreshold);
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
        if (ImGui::Button("Export CSV##pdw_export")) {
            exportToCsv(_exportPath);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &_autoScroll);

        ImGui::Separator();

        if (_totalPulses == 0) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No pulses yet. Press play, tune to a pulsed signal and set the threshold just above the noise floor.");
            ImGui::Spacing();
        }

        // PDW table
        int tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("##pdw_table", 8, tableFlags, ImVec2(0, 240))) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("TOA (s)", ImGuiTableColumnFlags_WidthFixed, 75.0f);
            ImGui::TableSetupColumn("PW (us)", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("dF (kHz)", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("Pk (dBFS)", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("Avg (dBFS)", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("SNR (dB)", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("PRI (us)", ImGuiTableColumnFlags_WidthFixed, 65.0f);
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
            }

            if (_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }

            ImGui::EndTable();
        }
    }
}
