#include "emitter_panel.h"
#include "../export/sigmf_exporter.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>

namespace rf_tools {

EmitterPanel::EmitterPanel() {}

void EmitterPanel::draw(EmitterTracker& tracker, double sampleRate) {
    if (ImGui::CollapsingHeader("Deinterleaving Parameters##emit_cfg", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool changed = false;

        if (ImGui::SliderFloat("Frequency Tolerance (kHz)##emit_ftol", &ui_freq_tol_khz_, 5.0f, 200.0f, "%.1f kHz")) {
            changed = true;
        }
        if (ImGui::SliderFloat("PW Tolerance (us)##emit_pwtol", &ui_pw_tol_us_, 0.2f, 10.0f, "%.2f us")) {
            changed = true;
        }
        if (ImGui::SliderFloat("DOA Tolerance (deg)##emit_doatol", &ui_doa_tol_deg_, 2.0f, 45.0f, "%.1f deg")) {
            changed = true;
        }
        if (ImGui::SliderFloat("Timeout (sec)##emit_tout", &ui_timeout_sec_, 1.0f, 30.0f, "%.1f s")) {
            tracker.setTimeout(ui_timeout_sec_);
        }

        if (changed) {
            tracker.setTolerances(ui_freq_tol_khz_ * 1.0e3, ui_pw_tol_us_, ui_doa_tol_deg_);
        }

        if (ImGui::Button("Reset All Tracks##emit_reset")) {
            tracker.reset();
            selected_emitter_id_ = -1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Export Tracks (JSON)##emit_export_json")) {
            SigMFExporter::exportJSON("recordings/emitter_tracks.json", {}, tracker.getEmitters(), sampleRate, 0.0);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    std::vector<TrackedEmitter> emitters = tracker.getEmitters();
    ImGui::Text("Active Emitters: %zu", emitters.size());

    int tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("##emitters_table", 9, tableFlags, ImVec2(0, 220))) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 35.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Freq (MHz)", ImGuiTableColumnFlags_WidthFixed, 85.0f);
        ImGui::TableSetupColumn("PW (us)", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("DOA (deg)", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("PRI Pattern", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("PRI Mean", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Pulses", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Avg (dBFS)", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableHeadersRow();

        for (const auto& e : emitters) {
            ImGui::TableNextRow();

            // ID column with selectable
            ImGui::TableSetColumnIndex(0);
            char id_label[16];
            std::snprintf(id_label, sizeof(id_label), "#%u", e.id);
            bool is_selected = (selected_emitter_id_ == static_cast<int>(e.id));
            if (ImGui::Selectable(id_label, is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                selected_emitter_id_ = static_cast<int>(e.id);
            }

            // Status column
            ImGui::TableSetColumnIndex(1);
            if (e.active) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "ACTIVE");
            } else {
                ImGui::TextDisabled("LOST");
            }

            // Frequency
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.4f", e.frequencyHz / 1.0e6);

            // PW
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f", e.pulseWidthSeconds * 1.0e6);

            // DOA
            ImGui::TableSetColumnIndex(4);
            if (e.hasDoa && e.doaAzimuthDeg >= 0.0f) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%.1f", e.doaAzimuthDeg);
            } else {
                ImGui::TextDisabled("--");
            }

            // PRI Pattern
            ImGui::TableSetColumnIndex(5);
            PRIStats stats;
            bool hasStats = false;
            if (e.priAnalyzer) {
                stats = e.priAnalyzer->getStats();
                hasStats = true;
                switch (stats.modulation) {
                    case PRIModulation::CONSTANT:
                        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "CONSTANT");
                        break;
                    case PRIModulation::STAGGERED:
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "STAGGER");
                        break;
                    case PRIModulation::JITTERED:
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "JITTER");
                        break;
                    default:
                        ImGui::TextDisabled("CALC...");
                        break;
                }
            } else {
                ImGui::TextDisabled("--");
            }

            // PRI Mean
            ImGui::TableSetColumnIndex(6);
            if (hasStats && stats.meanPri > 0.0) {
                ImGui::Text("%.1f us", stats.meanPri * 1.0e6);
            } else {
                ImGui::TextDisabled("--");
            }

            // Pulses
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%llu", (unsigned long long)e.pulseCount);

            // Power
            ImGui::TableSetColumnIndex(8);
            ImGui::Text("%.1f", e.averagePowerDbfs);
        }

        ImGui::EndTable();
    }

    // Selected emitter details
    if (selected_emitter_id_ > 0) {
        auto it = std::find_if(emitters.begin(), emitters.end(), [this](const TrackedEmitter& e) {
            return static_cast<int>(e.id) == selected_emitter_id_;
        });

        if (it != emitters.end()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Selected Emitter #%u Analysis", it->id);

            if (it->priAnalyzer) {
                PRIStats selStats = it->priAnalyzer->getStats();
                ImGui::BulletText("Mean PRI: %.2f us | Std Dev: %.2f us | PRF: %.1f Hz",
                                  selStats.meanPri * 1.0e6,
                                  selStats.stdDevPri * 1.0e6,
                                  selStats.estimatedPrf);

                if (selStats.modulation == PRIModulation::STAGGERED) {
                    const auto& levels = selStats.staggerLevels;
                    ImGui::Text("Stagger Levels (%zu):", levels.size());
                    for (size_t i = 0; i < levels.size(); ++i) {
                        ImGui::SameLine();
                        ImGui::Text("[%.1f us]", levels[i] * 1.0e6);
                    }
                }
            }
        }
    }
}

} // namespace rf_tools
