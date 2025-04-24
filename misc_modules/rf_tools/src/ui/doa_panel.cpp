#include "doa_panel.h"
#include <imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace rf_tools {

static constexpr float PI = 3.14159265358979323846f;
static constexpr float DEG_TO_RAD = PI / 180.0f;

DOAPanel::DOAPanel() {
    config_ = doa::DOAConfig{};
    ui_num_elements_ = static_cast<int>(config_.num_elements);
    ui_spacing_ = config_.element_spacing;
    ui_snapshots_ = static_cast<int>(config_.num_snapshots);
    ui_forgetting_ = config_.forgetting_factor;
    ui_enable_fbss_ = config_.enable_fbss;
    ui_expected_sources_ = static_cast<int>(config_.expected_sources);
    ui_auto_sources_ = config_.auto_model_order;
}

void DOAPanel::updateResult(const doa::DOAResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_result_ = result;
}

void DOAPanel::draw(doa::MUSICEngine& engine, doa::CovarianceAccumulator& accumulator) {
    doa::DOAResult result_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result_copy = latest_result_;
    }

    if (ImGui::CollapsingHeader("Array Configuration##doa_cfg", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool config_changed = false;

        const char* geometries[] = { "Uniform Linear Array (ULA)", "Uniform Circular Array (UCA)" };
        if (ImGui::Combo("Array Geometry##doa_geom", &ui_geometry_idx_, geometries, IM_ARRAYSIZE(geometries))) {
            config_.geometry = (ui_geometry_idx_ == 0) ? doa::ArrayGeometry::ULA : doa::ArrayGeometry::UCA;
            config_changed = true;
        }

        if (ImGui::SliderInt("Elements (M)##doa_m", &ui_num_elements_, 2, 16)) {
            config_.num_elements = static_cast<size_t>(ui_num_elements_);
            config_changed = true;
        }

        if (ImGui::SliderFloat("Spacing (d / lambda)##doa_d", &ui_spacing_, 0.1f, 1.0f, "%.2f")) {
            config_.element_spacing = ui_spacing_;
            config_changed = true;
        }

        if (ImGui::SliderInt("Snapshots (K)##doa_k", &ui_snapshots_, 32, 1024)) {
            config_.num_snapshots = static_cast<size_t>(ui_snapshots_);
            config_changed = true;
        }

        if (ImGui::SliderFloat("Forgetting Factor (alpha)##doa_alpha", &ui_forgetting_, 0.0f, 0.99f, "%.2f (0 = Block)")) {
            config_.forgetting_factor = ui_forgetting_;
            config_changed = true;
        }

        if (ImGui::Checkbox("Spatial Smoothing (FBSS)##doa_fbss", &ui_enable_fbss_)) {
            config_.enable_fbss = ui_enable_fbss_;
            config_changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Forward-Backward Spatial Smoothing to decorrelate coherent multipath signals");
        }

        ImGui::Checkbox("Auto Model Order (MDL)##doa_auto_mdl", &ui_auto_sources_);
        config_.auto_model_order = ui_auto_sources_;
        if (!ui_auto_sources_) {
            if (ImGui::SliderInt("Expected Sources (D)##doa_d_src", &ui_expected_sources_, 1, ui_num_elements_ - 1)) {
                config_.expected_sources = static_cast<size_t>(ui_expected_sources_);
                config_changed = true;
            }
        }

        if (config_changed) {
            engine.setConfig(config_);
            accumulator.init(config_.num_elements, config_.num_snapshots, config_.forgetting_factor, config_.enable_fbss);
        }
    }

    if (ImGui::CollapsingHeader("Calibration##doa_cal")) {
        ImGui::SliderFloat("Pilot Angle (deg)##doa_pilot", &ui_pilot_angle_, 0.0f, 180.0f, "%.1f deg");

        if (ImGui::Button("Calibrate from Current Covariance##doa_do_cal")) {
            Eigen::MatrixXcf current_R;
            if (accumulator.getNormalizedCovariance(current_R)) {
                current_cal_factors_ = engine.calibrateFromPilot(current_R, ui_pilot_angle_);
                accumulator.setCalibration(current_cal_factors_);
                cal_active_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Calibration##doa_reset_cal")) {
            accumulator.clearCalibration();
            current_cal_factors_.clear();
            cal_active_ = false;
        }

        if (cal_active_) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Calibration Active: %zu channels compensated", current_cal_factors_.size());
        } else {
            ImGui::TextDisabled("No calibration applied (ideal manifold assumed)");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Display polar compass and spectrum side by side or vertically depending on width
    float avail_width = ImGui::GetContentRegionAvail().x;
    float compass_size = std::min(avail_width * 0.45f, 220.0f);
    if (compass_size < 120.0f) compass_size = 120.0f;

    ImGui::Columns(2, "doa_columns", false);
    ImGui::SetColumnWidth(0, compass_size + 20.0f);

    ImGui::Text("Polar Direction");
    drawPolarCompass(result_copy.estimated_angles_deg, compass_size * 0.45f);

    ImGui::NextColumn();

    ImGui::Text("Detected Angles (Total: %zu)", result_copy.estimated_angles_deg.size());
    if (result_copy.estimated_angles_deg.empty()) {
        ImGui::TextDisabled("No direction of arrival detected");
    } else {
        for (size_t i = 0; i < result_copy.estimated_angles_deg.size(); ++i) {
            float angle = result_copy.estimated_angles_deg[i];
            ImGui::BulletText("Target %zu:  %.1f deg", i + 1, angle);
        }
    }

    ImGui::Spacing();
    ImGui::Text("MUSIC Pseudo-Spectrum (dB)");
    drawSpectrumPlot(result_copy.spectrum_angles_deg, result_copy.pseudo_spectrum_db);

    ImGui::Columns(1);
}

void DOAPanel::drawPolarCompass(const std::vector<float>& angles, float radius) {
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 center = ImVec2(cursor.x + radius + 10.0f, cursor.y + radius + 10.0f);

    // Background circle
    draw_list->AddCircleFilled(center, radius, IM_COL32(20, 25, 30, 255), 64);
    draw_list->AddCircle(center, radius, IM_COL32(70, 80, 90, 255), 64, 1.5f);
    draw_list->AddCircle(center, radius * 0.66f, IM_COL32(50, 60, 70, 180), 48, 1.0f);
    draw_list->AddCircle(center, radius * 0.33f, IM_COL32(50, 60, 70, 180), 48, 1.0f);

    // Axes lines
    draw_list->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), IM_COL32(60, 70, 80, 200), 1.0f);
    draw_list->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), IM_COL32(60, 70, 80, 200), 1.0f);

    // Direction labels (N, E, S, W)
    draw_list->AddText(ImVec2(center.x - 4.0f, center.y - radius - 15.0f), IM_COL32(180, 190, 200, 255), "N");
    draw_list->AddText(ImVec2(center.x + radius + 5.0f, center.y - 7.0f), IM_COL32(180, 190, 200, 255), "E");
    draw_list->AddText(ImVec2(center.x - 4.0f, center.y + radius + 2.0f), IM_COL32(180, 190, 200, 255), "S");
    draw_list->AddText(ImVec2(center.x - radius - 15.0f, center.y - 7.0f), IM_COL32(180, 190, 200, 255), "W");

    // Draw detected target needles
    static const ImU32 target_colors[] = {
        IM_COL32(255, 60, 60, 255),   // Red
        IM_COL32(60, 220, 60, 255),   // Green
        IM_COL32(60, 160, 255, 255),  // Blue
        IM_COL32(255, 200, 40, 255)   // Yellow
    };

    for (size_t i = 0; i < angles.size(); ++i) {
        float angle_rad = (angles[i] - 90.0f) * DEG_TO_RAD; // 0 deg is North
        ImVec2 needle_end = ImVec2(center.x + radius * 0.95f * std::cos(angle_rad),
                                   center.y + radius * 0.95f * std::sin(angle_rad));

        ImU32 col = target_colors[i % 4];
        draw_list->AddLine(center, needle_end, col, 2.5f);
        draw_list->AddCircleFilled(needle_end, 4.0f, col, 16);
    }

    // Center pin
    draw_list->AddCircleFilled(center, 3.5f, IM_COL32(220, 220, 220, 255), 16);

    ImGui::Dummy(ImVec2(radius * 2.0f + 20.0f, radius * 2.0f + 25.0f));
}

void DOAPanel::drawSpectrumPlot(const std::vector<float>& angles, const std::vector<float>& spectrum_db) {
    if (spectrum_db.empty() || angles.empty()) {
        ImGui::TextDisabled("Awaiting covariance accumulation...");
        return;
    }

    float width = ImGui::GetContentRegionAvail().x;
    float height = 110.0f;
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%.0f deg to %.0f deg", angles.front(), angles.back());

    ImGui::PlotLines("##doa_spectrum",
                     spectrum_db.data(),
                     static_cast<int>(spectrum_db.size()),
                     0,
                     overlay,
                     -40.0f,
                     0.0f,
                     ImVec2(width, height));
}

} // namespace rf_tools
