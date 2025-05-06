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

// Color helper for colormaps
static ImU32 getPaletteColor(float v, int palette) {
    v = std::clamp(v, 0.0f, 1.0f);
    if (palette == 0) {
        // Phosphor Green (Navy/Black -> Vivid Green -> Bright White-Green)
        int r = static_cast<int>(std::clamp(15.0f + 200.0f * v * v * v, 0.0f, 255.0f));
        int g = static_cast<int>(std::clamp(30.0f + 225.0f * v, 0.0f, 255.0f));
        int b = static_cast<int>(std::clamp(15.0f + 180.0f * v * v * v, 0.0f, 255.0f));
        return IM_COL32(r, g, b, 255);
    } else if (palette == 1) {
        // Amber Radar (Dark Brown -> Orange Amber -> Bright Yellow-White)
        int r = static_cast<int>(std::clamp(25.0f + 230.0f * v, 0.0f, 255.0f));
        int g = static_cast<int>(std::clamp(15.0f + 185.0f * std::pow(v, 1.4f), 0.0f, 255.0f));
        int b = static_cast<int>(std::clamp(5.0f + 160.0f * v * v * v, 0.0f, 255.0f));
        return IM_COL32(r, g, b, 255);
    } else {
        // Turbo Colormap Polynomial Approximation
        float r = 0.1357f + v * (4.597f + v * (-42.34f + v * (130.5f + v * (-150.5f + v * 58.13f))));
        float g = 0.0914f + v * (2.185f + v * (4.805f + v * (-35.33f + v * (60.63f + v * -31.33f))));
        float b = 0.1067f + v * (12.58f + v * (-78.48f + v * (213.8f + v * (-262.1f + v * 114.7f))));
        return IM_COL32(
            static_cast<int>(std::clamp(r * 255.0f, 0.0f, 255.0f)),
            static_cast<int>(std::clamp(g * 255.0f, 0.0f, 255.0f)),
            static_cast<int>(std::clamp(b * 255.0f, 0.0f, 255.0f)),
            255
        );
    }
}

DOAPanel::DOAPanel() {
    config_ = doa::DOAConfig{};
    ui_num_elements_ = static_cast<int>(config_.num_elements);
    ui_spacing_ = config_.element_spacing;
    ui_snapshots_ = static_cast<int>(config_.num_snapshots);
    ui_forgetting_ = config_.forgetting_factor;
    ui_enable_fbss_ = config_.enable_fbss;
    ui_expected_sources_ = static_cast<int>(config_.expected_sources);
    ui_auto_sources_ = config_.auto_model_order;
    last_sweep_time_ = std::chrono::steady_clock::now();
}

void DOAPanel::updateResult(const doa::DOAResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_result_ = result;

    auto now = std::chrono::steady_clock::now();

    // Append to waterfall history if spectrum is valid
    if (!result.pseudo_spectrum_db.empty()) {
        WaterfallRow row;
        row.spectrumDb = result.pseudo_spectrum_db;
        row.anglesDeg = result.spectrum_angles_deg;
        row.timestamp = now;
        waterfall_history_.push_front(std::move(row));
        if (waterfall_history_.size() > max_waterfall_rows_) {
            waterfall_history_.pop_back();
        }
    }

    // Add phosphor blips for detected targets
    for (float angle : result.estimated_angles_deg) {
        PhosphorBlip blip;
        blip.angleDeg = angle;
        blip.powerDbfs = -25.0f;
        blip.intensity = 1.0f;
        blip.timestamp = now;
        phosphor_blips_.push_back(blip);
    }

    // Limit active blips
    while (phosphor_blips_.size() > 64) {
        phosphor_blips_.pop_front();
    }
}

void DOAPanel::addPulseHit(float angleDeg, float powerDbfs) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    PhosphorBlip blip;
    blip.angleDeg = angleDeg;
    blip.powerDbfs = powerDbfs;
    blip.intensity = 1.0f;
    blip.timestamp = now;
    phosphor_blips_.push_back(blip);

    while (phosphor_blips_.size() > 64) {
        phosphor_blips_.pop_front();
    }
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

    // Display mode selector
    const char* display_modes[] = { "Comprehensive (All)", "Polar Compass", "Radar PPI (Phosphor)", "Azimuth Waterfall" };
    if (ImGui::Combo("Display Mode##doa_disp_mode", &ui_display_mode_idx_, display_modes, IM_ARRAYSIZE(display_modes))) {
        display_mode_ = static_cast<DOADisplayMode>(ui_display_mode_idx_);
    }

    float avail_width = ImGui::GetContentRegionAvail().x;
    float radar_radius = std::clamp(avail_width * 0.22f, 110.0f, 175.0f);

    if (display_mode_ == DOADisplayMode::ALL || display_mode_ == DOADisplayMode::PPI_RADAR) {
        ImGui::Checkbox("Radar Sweep##doa_swp_en", &sweep_enabled_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("RPM##doa_swp_rpm", &sweep_rpm_, 5.0f, 60.0f, "%.0f RPM");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("Phosphor Decay##doa_p_decay", &phosphor_decay_sec_, 0.5f, 10.0f, "%.1f s");
    }

    if (display_mode_ == DOADisplayMode::ALL) {
        ImGui::Columns(2, "doa_all_cols", false);
        ImGui::SetColumnWidth(0, radar_radius * 2.0f + 30.0f);

        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Tactical PPI Radar");
        drawRadarPPI(result_copy.estimated_angles_deg, radar_radius);

        ImGui::NextColumn();

        ImGui::Text("Detected Targets (%zu)", result_copy.estimated_angles_deg.size());
        if (result_copy.estimated_angles_deg.empty()) {
            ImGui::TextDisabled("Searching azimuth wavefronts...");
        } else {
            for (size_t i = 0; i < result_copy.estimated_angles_deg.size(); ++i) {
                float angle = result_copy.estimated_angles_deg[i];
                ImGui::BulletText("Target %zu:  %.2f deg", i + 1, angle);
            }
        }

        ImGui::Spacing();
        ImGui::Text("Azimuth vs Time Waterfall");
        drawAzimuthWaterfall(ImGui::GetContentRegionAvail().x, 130.0f);

        ImGui::Spacing();
        ImGui::Text("Spatial MUSIC Pseudo-Spectrum (dB)");
        drawSpectrumPlot(result_copy.spectrum_angles_deg, result_copy.pseudo_spectrum_db);

        ImGui::Columns(1);
    } else if (display_mode_ == DOADisplayMode::POLAR_COMPASS) {
        ImGui::Text("Polar Compass View");
        drawPolarCompass(result_copy.estimated_angles_deg, radar_radius);

        ImGui::Spacing();
        ImGui::Text("Detected Targets (%zu)", result_copy.estimated_angles_deg.size());
        for (size_t i = 0; i < result_copy.estimated_angles_deg.size(); ++i) {
            ImGui::BulletText("Target %zu:  %.2f deg", i + 1, result_copy.estimated_angles_deg[i]);
        }
        drawSpectrumPlot(result_copy.spectrum_angles_deg, result_copy.pseudo_spectrum_db);
    } else if (display_mode_ == DOADisplayMode::PPI_RADAR) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Tactical PPI Radar (Phosphor Display)");
        drawRadarPPI(result_copy.estimated_angles_deg, radar_radius * 1.35f);

        ImGui::Spacing();
        ImGui::Text("Detected Targets (%zu)", result_copy.estimated_angles_deg.size());
        for (size_t i = 0; i < result_copy.estimated_angles_deg.size(); ++i) {
            ImGui::BulletText("Target %zu:  %.2f deg", i + 1, result_copy.estimated_angles_deg[i]);
        }
    } else if (display_mode_ == DOADisplayMode::WATERFALL) {
        ImGui::Text("Azimuth vs Time Waterfall (Spatiotemporal History)");
        const char* palettes[] = { "Phosphor Green", "Amber Radar", "Turbo Colormap" };
        ImGui::Combo("Palette##wf_pal", &waterfall_palette_, palettes, IM_ARRAYSIZE(palettes));
        drawAzimuthWaterfall(avail_width, 240.0f);

        ImGui::Spacing();
        drawSpectrumPlot(result_copy.spectrum_angles_deg, result_copy.pseudo_spectrum_db);
    }
}

void DOAPanel::drawPolarCompass(const std::vector<float>& angles, float radius) {
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 center = ImVec2(cursor.x + radius + 10.0f, cursor.y + radius + 10.0f);

    // Background circle
    draw_list->AddCircleFilled(center, radius, IM_COL32(18, 24, 28, 255), 64);
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

    // Needles
    static const ImU32 target_colors[] = {
        IM_COL32(255, 60, 60, 255),
        IM_COL32(60, 220, 60, 255),
        IM_COL32(60, 160, 255, 255),
        IM_COL32(255, 200, 40, 255)
    };

    for (size_t i = 0; i < angles.size(); ++i) {
        float angle_rad = (angles[i] - 90.0f) * DEG_TO_RAD;
        ImVec2 needle_end = ImVec2(center.x + radius * 0.95f * std::cos(angle_rad),
                                   center.y + radius * 0.95f * std::sin(angle_rad));

        ImU32 col = target_colors[i % 4];
        draw_list->AddLine(center, needle_end, col, 2.5f);
        draw_list->AddCircleFilled(needle_end, 4.0f, col, 16);
    }

    draw_list->AddCircleFilled(center, 3.5f, IM_COL32(220, 220, 220, 255), 16);
    ImGui::Dummy(ImVec2(radius * 2.0f + 20.0f, radius * 2.0f + 25.0f));
}

void DOAPanel::drawRadarPPI(const std::vector<float>& angles, float radius) {
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 center = ImVec2(cursor.x + radius + 15.0f, cursor.y + radius + 15.0f);
    auto now = std::chrono::steady_clock::now();

    // Update sweep rotation
    std::chrono::duration<float> elapsed = now - last_sweep_time_;
    last_sweep_time_ = now;
    float dt = std::clamp(elapsed.count(), 0.0f, 0.1f);

    if (sweep_enabled_) {
        float sweep_speed_deg_per_sec = 360.0f * (sweep_rpm_ / 60.0f);
        current_sweep_angle_ = std::fmod(current_sweep_angle_ + sweep_speed_deg_per_sec * dt, 360.0f);
    }

    // Outer CRT bezel and dark phosphorescent screen
    draw_list->AddCircleFilled(center, radius + 2.0f, IM_COL32(8, 22, 14, 255), 64);
    draw_list->AddCircle(center, radius + 2.0f, IM_COL32(35, 75, 45, 255), 64, 2.5f);

    // Range rings (25%, 50%, 75%, 100%)
    const float ring_ratios[] = { 0.25f, 0.50f, 0.75f, 1.0f };
    const char* ring_labels[] = { "-60dB", "-40dB", "-20dB", "0dB" };
    for (int i = 0; i < 4; ++i) {
        float r = radius * ring_ratios[i];
        draw_list->AddCircle(center, r, IM_COL32(25, 65, 35, 140), 64, 1.0f);
        if (radius > 80.0f) {
            draw_list->AddText(ImVec2(center.x + 4.0f, center.y - r - 2.0f), IM_COL32(60, 130, 75, 180), ring_labels[i]);
        }
    }

    // Radial spokes and degree marks every 30 degrees
    for (int deg = 0; deg < 360; deg += 30) {
        float rad = (static_cast<float>(deg) - 90.0f) * DEG_TO_RAD;
        float cos_a = std::cos(rad);
        float sin_a = std::sin(rad);

        ImVec2 p_inner = ImVec2(center.x + (radius * 0.15f) * cos_a, center.y + (radius * 0.15f) * sin_a);
        ImVec2 p_outer = ImVec2(center.x + radius * cos_a, center.y + radius * sin_a);
        draw_list->AddLine(p_inner, p_outer, IM_COL32(20, 60, 30, 130), 1.0f);

        // Degree label outside ring
        if (radius > 95.0f && (deg % 30 == 0)) {
            char deg_text[8];
            std::snprintf(deg_text, sizeof(deg_text), "%03d", deg);
            ImVec2 p_text = ImVec2(center.x + (radius + 8.0f) * cos_a - 9.0f,
                                   center.y + (radius + 8.0f) * sin_a - 6.0f);
            draw_list->AddText(p_text, IM_COL32(70, 160, 85, 220), deg_text);
        }
    }

    // Rotating Phosphor Sweep Trail
    if (sweep_enabled_) {
        // Trailing fading sector behind sweep beam (phosphor persistence trail)
        const int trail_slices = 18;
        const float trail_span_deg = 35.0f;
        float slice_deg = trail_span_deg / static_cast<float>(trail_slices);

        for (int s = 0; s < trail_slices; ++s) {
            float a1 = (current_sweep_angle_ - static_cast<float>(s) * slice_deg - 90.0f) * DEG_TO_RAD;
            float a2 = (current_sweep_angle_ - static_cast<float>(s + 1) * slice_deg - 90.0f) * DEG_TO_RAD;

            float fade = 1.0f - (static_cast<float>(s) / static_cast<float>(trail_slices));
            int alpha = static_cast<int>(std::clamp(fade * fade * 55.0f, 0.0f, 255.0f));

            ImVec2 p1 = ImVec2(center.x + radius * std::cos(a1), center.y + radius * std::sin(a1));
            ImVec2 p2 = ImVec2(center.x + radius * std::cos(a2), center.y + radius * std::sin(a2));

            draw_list->AddTriangleFilled(center, p1, p2, IM_COL32(30, 200, 60, alpha));
        }

        // Leading bright sweep beam line
        float sweep_rad = (current_sweep_angle_ - 90.0f) * DEG_TO_RAD;
        ImVec2 sweep_tip = ImVec2(center.x + radius * std::cos(sweep_rad), center.y + radius * std::sin(sweep_rad));
        draw_list->AddLine(center, sweep_tip, IM_COL32(140, 255, 160, 240), 2.0f);
    }

    // Draw Persisting Phosphor Blips
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = phosphor_blips_.begin();
        while (it != phosphor_blips_.end()) {
            std::chrono::duration<float> age = now - it->timestamp;
            float age_sec = age.count();

            if (age_sec > phosphor_decay_sec_) {
                it = phosphor_blips_.erase(it);
                continue;
            }

            float decay = std::clamp(1.0f - (age_sec / phosphor_decay_sec_), 0.0f, 1.0f);
            float blip_rad = (it->angleDeg - 90.0f) * DEG_TO_RAD;

            // Power mapping (-80 dBFS to 0 dBFS -> 0.3 to 0.95 radius)
            float norm_pwr = std::clamp((it->powerDbfs + 80.0f) / 80.0f, 0.25f, 1.0f);
            float blip_dist = radius * (0.35f + 0.55f * norm_pwr);

            ImVec2 blip_pos = ImVec2(center.x + blip_dist * std::cos(blip_rad),
                                     center.y + blip_dist * std::sin(blip_rad));

            // Outer bloom halo
            int halo_alpha = static_cast<int>(decay * 80.0f);
            draw_list->AddCircleFilled(blip_pos, 10.0f, IM_COL32(20, 220, 50, halo_alpha), 16);

            // Mid phosphor body
            int mid_alpha = static_cast<int>(decay * 180.0f);
            draw_list->AddCircleFilled(blip_pos, 5.5f, IM_COL32(50, 255, 90, mid_alpha), 16);

            // Core bright phosphor white-green
            int core_alpha = static_cast<int>(decay * 255.0f);
            draw_list->AddCircleFilled(blip_pos, 2.5f, IM_COL32(220, 255, 230, core_alpha), 12);

            // Radial strobe ray
            int ray_alpha = static_cast<int>(decay * 60.0f);
            draw_list->AddLine(center, blip_pos, IM_COL32(30, 200, 70, ray_alpha), 1.0f);

            ++it;
        }
    }

    // Draw Currently Tracked Target Needles/Strobes from MUSIC
    static const ImU32 target_colors[] = {
        IM_COL32(255, 70, 70, 240),
        IM_COL32(50, 255, 120, 240),
        IM_COL32(60, 180, 255, 240),
        IM_COL32(255, 215, 50, 240)
    };

    for (size_t i = 0; i < angles.size(); ++i) {
        float angle_rad = (angles[i] - 90.0f) * DEG_TO_RAD;
        ImVec2 strobe_end = ImVec2(center.x + radius * 0.96f * std::cos(angle_rad),
                                   center.y + radius * 0.96f * std::sin(angle_rad));

        ImU32 col = target_colors[i % 4];
        draw_list->AddLine(center, strobe_end, col, 1.8f);

        // Target diamond marker
        float sz = 4.5f;
        draw_list->AddQuadFilled(
            ImVec2(strobe_end.x, strobe_end.y - sz),
            ImVec2(strobe_end.x + sz, strobe_end.y),
            ImVec2(strobe_end.x, strobe_end.y + sz),
            ImVec2(strobe_end.x - sz, strobe_end.y),
            col
        );

        if (radius > 85.0f) {
            char tag[16];
            std::snprintf(tag, sizeof(tag), "T%zu:%.1f*", i + 1, angles[i]);
            draw_list->AddText(ImVec2(strobe_end.x + 6.0f, strobe_end.y - 6.0f), col, tag);
        }
    }

    // Center CRT pivot
    draw_list->AddCircleFilled(center, 4.0f, IM_COL32(180, 255, 200, 255), 16);
    draw_list->AddCircle(center, 4.0f, IM_COL32(20, 60, 30, 255), 16, 1.0f);

    ImGui::Dummy(ImVec2(radius * 2.0f + 30.0f, radius * 2.0f + 30.0f));
}

void DOAPanel::drawAzimuthWaterfall(float width, float height) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (waterfall_history_.empty()) {
        ImGui::TextDisabled("Awaiting spatial spectrum frames for waterfall...");
        return;
    }

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Background rect
    draw_list->AddRectFilled(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(10, 14, 18, 255));
    draw_list->AddRect(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(40, 60, 75, 255));

    size_t num_rows = std::min(waterfall_history_.size(), max_waterfall_rows_);
    if (num_rows == 0) return;

    float row_height = height / static_cast<float>(num_rows);

    for (size_t r = 0; r < num_rows; ++r) {
        const auto& row = waterfall_history_[r];
        if (row.spectrumDb.empty()) continue;

        size_t num_cols = row.spectrumDb.size();
        float col_width = width / static_cast<float>(num_cols);

        float y0 = cursor.y + static_cast<float>(r) * row_height;
        float y1 = y0 + row_height;

        for (size_t c = 0; c < num_cols; ++c) {
            float pwr = row.spectrumDb[c];
            float norm = std::clamp((pwr - waterfall_min_db_) / (waterfall_max_db_ - waterfall_min_db_), 0.0f, 1.0f);

            ImU32 col = getPaletteColor(norm, waterfall_palette_);
            float x0 = cursor.x + static_cast<float>(c) * col_width;
            float x1 = x0 + col_width;

            draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col);
        }
    }

    // Border line at bottom
    draw_list->AddLine(ImVec2(cursor.x, cursor.y + height), ImVec2(cursor.x + width, cursor.y + height), IM_COL32(60, 90, 110, 255), 1.0f);

    // Degree axis ticks
    float min_angle = waterfall_history_.front().anglesDeg.empty() ? 0.0f : waterfall_history_.front().anglesDeg.front();
    float max_angle = waterfall_history_.front().anglesDeg.empty() ? 180.0f : waterfall_history_.front().anglesDeg.back();

    char label_min[16], label_mid[16], label_max[16];
    std::snprintf(label_min, sizeof(label_min), "%.0f*", min_angle);
    std::snprintf(label_mid, sizeof(label_mid), "%.0f*", (min_angle + max_angle) * 0.5f);
    std::snprintf(label_max, sizeof(label_max), "%.0f*", max_angle);

    draw_list->AddText(ImVec2(cursor.x + 2.0f, cursor.y + height + 2.0f), IM_COL32(160, 180, 200, 220), label_min);
    draw_list->AddText(ImVec2(cursor.x + width * 0.5f - 10.0f, cursor.y + height + 2.0f), IM_COL32(160, 180, 200, 220), label_mid);
    draw_list->AddText(ImVec2(cursor.x + width - 30.0f, cursor.y + height + 2.0f), IM_COL32(160, 180, 200, 220), label_max);

    ImGui::Dummy(ImVec2(width, height + 18.0f));

    // Interactive hover inspection
    if (ImGui::IsItemHovered()) {
        ImVec2 mouse = ImGui::GetMousePos();
        float rel_x = (mouse.x - cursor.x) / width;
        float hovered_angle = min_angle + rel_x * (max_angle - min_angle);
        ImGui::SetTooltip("Azimuth: %.1f deg\nLive spatiotemporal history", hovered_angle);
    }
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
