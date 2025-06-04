#include "vna_panel.h"
#include <gui/gui.h>
#include <gui/style.h>
#include <imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace rf_tools {

VNAPanel::VNAPanel() {}

void VNAPanel::draw(vna::VNAAnalyzer& analyzer) {
    auto trace = analyzer.getTrace();
    auto fit = analyzer.getDipFit();
    auto cfg = analyzer.getConfig();

    float availWidth = ImGui::GetContentRegionAvail().x;

    // Controls
    if (ImGui::CollapsingHeader("VNA Sweep & Resonator Setup##vna_ctrl_hdr", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::LeftLabel("Start Freq (MHz)");
        ImGui::SetNextItemWidth(availWidth - ImGui::GetCursorPosX());
        if (ImGui::InputFloat("##vna_f_start", &ui_start_mhz_, 0.5f, 5.0f, "%.3f")) {
            cfg.startFreqHz = ui_start_mhz_ * 1e6;
            analyzer.setConfig(cfg);
        }

        ImGui::LeftLabel("Stop Freq (MHz)");
        ImGui::SetNextItemWidth(availWidth - ImGui::GetCursorPosX());
        if (ImGui::InputFloat("##vna_f_stop", &ui_stop_mhz_, 0.5f, 5.0f, "%.3f")) {
            cfg.stopFreqHz = ui_stop_mhz_ * 1e6;
            analyzer.setConfig(cfg);
        }

        ImGui::LeftLabel("Points");
        ImGui::SetNextItemWidth(availWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderInt("##vna_n_pts", &ui_points_, 51, 1001)) {
            cfg.numPoints = static_cast<size_t>(ui_points_);
            analyzer.setConfig(cfg);
        }

        if (ImGui::Checkbox("Detect Dip / Notch (MKID Resonator)##vna_find_dips", &ui_find_dips_)) {
            cfg.findDips = ui_find_dips_;
            analyzer.setConfig(cfg);
        }

        ImGui::Spacing();
        if (ImGui::Button("Simulate JPL Resonator Sweep##vna_sim_btn")) {
            double f0 = (ui_start_mhz_ + ui_stop_mhz_) * 0.5 * 1e6;
            analyzer.generateSyntheticResonatorSweep(f0, ui_synth_q_, ui_synth_depth_);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("Sim Q##vna_sim_q", &ui_synth_q_, 500.0f, 10000.0f, "Q = %.0f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::SliderFloat("Dip (dB)##vna_sim_d", &ui_synth_depth_, 5.0f, 40.0f, "%.0f dB");
    }

    // Results Badge
    ImGui::Spacing();
    if (fit.valid) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f),
            "[RESONANCE] f0 = %.4f MHz | BW(-3dB) = %.1f kHz | Dip = %.1f dB",
            fit.resonantFreqHz / 1e6, fit.bandwidth3dbHz / 1e3, fit.dipDepthDb);
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f),
            "Loaded Q (Q_L): %.0f | Internal Q (Q_i): %.0f",
            fit.loadedQ, fit.internalQ);
    } else {
        ImGui::TextDisabled("Awaiting resonance fit (minimum 2 dB transmission dip required)...");
    }
    ImGui::Separator();

    // Plot tabs
    if (ImGui::BeginTabBar("##vna_plot_tabs")) {
        if (ImGui::BeginTabItem("S21 Transmission Magnitude (|S21|)")) {
            drawMagnitudePlot(trace, fit);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("S21 Transmission Phase (deg)")) {
            drawPhasePlot(trace);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void VNAPanel::drawMagnitudePlot(const std::vector<vna::VNASweepPoint>& trace, const vna::ResonatorDipFit& fit) {
    float width = ImGui::GetContentRegionAvail().x;
    float height = 220.0f;

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(12, 16, 22, 255));
    draw_list->AddRect(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(45, 60, 80, 255));

    if (trace.size() < 2) return;

    // Y scale: from 0 dB down to -40 dB
    float minDb = -35.0f;
    float maxDb = 5.0f;
    for (const auto& pt : trace) {
        if (pt.magnitudeDb < minDb) minDb = pt.magnitudeDb - 5.0f;
        if (pt.magnitudeDb > maxDb) maxDb = pt.magnitudeDb + 2.0f;
    }

    auto mapCoord = [&](size_t idx, float db) -> ImVec2 {
        float x = cursor.x + (static_cast<float>(idx) / static_cast<float>(trace.size() - 1)) * width;
        float y = cursor.y + (1.0f - (db - minDb) / (maxDb - minDb + 1e-6f)) * height;
        return ImVec2(x, y);
    };

    // Grid lines
    for (float db = 0.0f; db >= minDb; db -= 10.0f) {
        float y = cursor.y + (1.0f - (db - minDb) / (maxDb - minDb + 1e-6f)) * height;
        draw_list->AddLine(ImVec2(cursor.x, y), ImVec2(cursor.x + width, y), IM_COL32(40, 50, 65, 120));
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%+.0f dB", db);
        draw_list->AddText(ImVec2(cursor.x + 4, y - 14), IM_COL32(120, 150, 180, 200), buf);
    }

    // Plot S21 curve
    ImVec2 pPrev;
    for (size_t i = 0; i < trace.size(); ++i) {
        ImVec2 pCurr = mapCoord(i, trace[i].magnitudeDb);
        if (i > 0) {
            draw_list->AddLine(pPrev, pCurr, IM_COL32(50, 180, 255, 255), 2.0f);
        }
        pPrev = pCurr;
    }

    // Highlight resonance dip
    if (fit.valid) {
        double fMin = trace.front().freqHz, fMax = trace.back().freqHz;
        float xDip = cursor.x + static_cast<float>((fit.resonantFreqHz - fMin) / (fMax - fMin + 1e-9)) * width;
        draw_list->AddLine(ImVec2(xDip, cursor.y), ImVec2(xDip, cursor.y + height), IM_COL32(255, 80, 80, 180), 1.5f);

        char markerBuf[48];
        std::snprintf(markerBuf, sizeof(markerBuf), "f0: %.3f MHz", fit.resonantFreqHz / 1e6);
        draw_list->AddText(ImVec2(xDip + 6, cursor.y + height - 20), IM_COL32(255, 100, 100, 255), markerBuf);
    }

    ImGui::Dummy(ImVec2(width, height + 6.0f));
}

void VNAPanel::drawPhasePlot(const std::vector<vna::VNASweepPoint>& trace) {
    float width = ImGui::GetContentRegionAvail().x;
    float height = 200.0f;

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(12, 16, 22, 255));
    draw_list->AddRect(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(45, 60, 80, 255));

    if (trace.size() < 2) return;

    float minPhase = -180.0f;
    float maxPhase = 180.0f;

    auto mapCoord = [&](size_t idx, float deg) -> ImVec2 {
        float x = cursor.x + (static_cast<float>(idx) / static_cast<float>(trace.size() - 1)) * width;
        float y = cursor.y + (1.0f - (deg - minPhase) / (maxPhase - minPhase + 1e-6f)) * height;
        return ImVec2(x, y);
    };

    // Zero phase line
    float midY = cursor.y + height * 0.5f;
    draw_list->AddLine(ImVec2(cursor.x, midY), ImVec2(cursor.x + width, midY), IM_COL32(50, 70, 90, 180), 1.0f);

    ImVec2 pPrev;
    for (size_t i = 0; i < trace.size(); ++i) {
        ImVec2 pCurr = mapCoord(i, trace[i].phaseDeg);
        if (i > 0) {
            draw_list->AddLine(pPrev, pCurr, IM_COL32(255, 170, 50, 240), 1.8f);
        }
        pPrev = pCurr;
    }

    ImGui::Dummy(ImVec2(width, height + 6.0f));
}

} // namespace rf_tools
