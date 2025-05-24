#include "beamformer_panel.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace rf_tools {

static constexpr float PI = 3.14159265358979323846f;
static constexpr float DEG_TO_RAD = PI / 180.0f;

BeamformerPanel::BeamformerPanel() {
    active_nulls_deg_.push_back(40.0f);
}

ImU32 BeamformerPanel::getTurboColor(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float r = std::clamp(0.1357f + t * (4.5155f + t * (-8.9837f + t * (9.8764f + t * -4.5434f))), 0.0f, 1.0f);
    const float g = std::clamp(0.0914f + t * (2.1855f + t * (4.8052f + t * (-14.0195f + t * 7.0786f))), 0.0f, 1.0f);
    const float b = std::clamp(0.1067f + t * (12.5732f + t * (-30.1581f + t * (33.4477f + t * -14.9795f))), 0.0f, 1.0f);
    return IM_COL32(static_cast<int>(r * 255.0f), static_cast<int>(g * 255.0f), static_cast<int>(b * 255.0f), 255);
}

void BeamformerPanel::draw(beamforming::AdaptiveBeamformer& beamformer,
                           doa::CovarianceAccumulator& accumulator,
                           const DOAPanel& doaPanel,
                           const EmitterTracker& emitterTracker) {
    const auto& doa_cfg = doaPanel.getConfig();
    beamforming::BeamformerConfig bf_cfg = beamformer.getConfig();

    bf_cfg.geometry = doa_cfg.geometry;
    bf_cfg.num_elements = doa_cfg.num_elements;
    bf_cfg.element_spacing = doa_cfg.element_spacing;
    bf_cfg.num_elements_x = doa_cfg.num_elements_x;
    bf_cfg.num_elements_y = doa_cfg.num_elements_y;
    bf_cfg.element_spacing_y = doa_cfg.element_spacing_y;

    // 1. Controls Header
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Adaptive Beamforming & Spatial Filtering Engine");
    ImGui::Separator();

    const char* algo_names[] = {
        "Delay-and-Sum (Bartlett)",
        "Adaptive MVDR (Capon Null Steering)",
        "Robust MVDR (Diagonal Loaded LSMI)",
        "LCMV (Explicit Multi-Null Steering)"
    };

    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::Combo("Algorithm##bf_algo", &selected_algo_idx_, algo_names, IM_ARRAYSIZE(algo_names))) {
        bf_cfg.algorithm = static_cast<beamforming::BeamformerAlgorithm>(selected_algo_idx_);
    }

    if (selected_algo_idx_ == 2) {
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderFloat("Diagonal Loading##bf_loading", &diag_loading_factor_, 1e-5f, 1e-1f, "%.5f", ImGuiSliderFlags_Logarithmic)) {
            bf_cfg.diagonal_loading_factor = diag_loading_factor_;
        }
    }

    ImGui::Spacing();

    // 2. Look Direction Configuration
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Desired Look Direction (SOI)");
    float max_az = (bf_cfg.geometry == doa::ArrayGeometry::ULA) ? 180.0f : 360.0f;
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::SliderFloat("Look Azimuth (deg)##bf_look_az", &look_azimuth_deg_, 0.0f, max_az, "%.1f deg")) {
        auto_track_target_ = false;
    }

    if (bf_cfg.geometry == doa::ArrayGeometry::URA) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderFloat("Look Elevation (deg)##bf_look_el", &look_elevation_deg_, 0.0f, 90.0f, "%.1f deg");
    }

    // Quick-Steer to detected targets
    const auto& doa_res = doaPanel.getLastResult();
    if (!doa_res.estimated_angles_deg.empty()) {
        ImGui::TextDisabled("Detected DOA Targets:");
        ImGui::SameLine();
        for (size_t i = 0; i < std::min(doa_res.estimated_angles_deg.size(), (size_t)4); ++i) {
            float tgt_ang = doa_res.estimated_angles_deg[i];
            char btn_lbl[32];
            snprintf(btn_lbl, sizeof(btn_lbl), "Target %zu (%.1f deg)##bf_tgt", i + 1, tgt_ang);
            if (ImGui::SmallButton(btn_lbl)) {
                look_azimuth_deg_ = tgt_ang;
                auto_track_target_ = false;
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }

    // Emitter tracking integration
    const auto& emitters = emitterTracker.getEmitters();
    if (!emitters.empty()) {
        ImGui::Checkbox("Auto-Track Selected Emitter##bf_autotrack", &auto_track_target_);
        if (auto_track_target_) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::BeginCombo("Emitter##bf_track_em", tracked_emitter_id_ >= 0 ? ("ID " + std::to_string(tracked_emitter_id_)).c_str() : "Select Emitter")) {
                for (const auto& em : emitters) {
                    bool is_sel = (static_cast<int>(em.id) == tracked_emitter_id_);
                    char em_desc[64];
                    snprintf(em_desc, sizeof(em_desc), "ID %u: %.1f MHz | DOA: %.1f deg", em.id, em.frequencyHz / 1e6, em.doaAzimuthDeg);
                    if (ImGui::Selectable(em_desc, is_sel)) {
                        tracked_emitter_id_ = static_cast<int>(em.id);
                        if (em.hasDoa) {
                            look_azimuth_deg_ = em.doaAzimuthDeg;
                        }
                    }
                }
                ImGui::EndCombo();
            }

            for (const auto& em : emitters) {
                if (static_cast<int>(em.id) == tracked_emitter_id_) {
                    if (em.hasDoa) {
                        look_azimuth_deg_ = em.doaAzimuthDeg;
                    }
                    break;
                }
            }
        }
    }

    ImGui::Spacing();

    // 3. Null Steering (Interference / Jammer Cancellation)
    if (selected_algo_idx_ == 3 || selected_algo_idx_ == 1 || selected_algo_idx_ == 2) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Interference Cancellation & Null Steering");

        if (selected_algo_idx_ == 3) {
            // Explicit nulls for LCMV
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputFloat("Angle (deg)##bf_new_null", &new_null_deg_, 1.0f, 5.0f, "%.1f");
            ImGui::SameLine();
            if (ImGui::Button("Add Null Angle##bf_add_null")) {
                active_nulls_deg_.push_back(std::clamp(new_null_deg_, 0.0f, max_az));
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear All Nulls##bf_clear_nulls")) {
                active_nulls_deg_.clear();
            }

            // Display current nulls list
            for (size_t i = 0; i < active_nulls_deg_.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                char null_str[32];
                snprintf(null_str, sizeof(null_str), "Null %zu (deg)", i + 1);
                ImGui::SetNextItemWidth(140.0f);
                ImGui::SliderFloat(null_str, &active_nulls_deg_[i], 0.0f, max_az, "%.1f deg");
                ImGui::SameLine();
                if (ImGui::SmallButton("X##remove_null")) {
                    active_nulls_deg_.erase(active_nulls_deg_.begin() + i);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
        } else {
            ImGui::TextDisabled("MVDR automatically detects and steers deep nulls to all active jammers.");
        }
    }

    // 4. Update Beamformer Config & Compute Weights
    bf_cfg.look_azimuth_deg = look_azimuth_deg_;
    bf_cfg.look_elevation_deg = look_elevation_deg_;
    bf_cfg.null_azimuths_deg = active_nulls_deg_;
    bf_cfg.algorithm = static_cast<beamforming::BeamformerAlgorithm>(selected_algo_idx_);
    bf_cfg.diagonal_loading_factor = diag_loading_factor_;
    beamformer.setConfig(bf_cfg);

    Eigen::MatrixXcf Rxx;
    if (accumulator.getNormalizedCovariance(Rxx)) {
        beamformer.computeWeights(Rxx);
    }
    const auto& res = beamformer.getLastResult();

    // 5. Status / Metrics Bar
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Look Gain: %.2f dB", res.look_gain_db);
    ImGui::SameLine(180.0f);
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Array Power: %.2f dBFS", 10.0f * std::log10(std::max(res.output_power, 1e-6f)));

    if (!res.null_gains_db.empty()) {
        ImGui::SameLine(360.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Deepest Null: %.1f dB",
            *std::min_element(res.null_gains_db.begin(), res.null_gains_db.end()));
    }

    // Audio / VFO filter pass-through toggle
    ImGui::Checkbox("Apply Spatial Filter to Demodulator Audio##bf_demod_filt", &filter_audio_enabled_);

    // 6. Visualizations
    ImGui::Spacing();
    ImGui::Checkbox("Polar Beampattern##bf_show_polar", &show_polar_plot_);
    ImGui::SameLine();
    ImGui::Checkbox("Cartesian Gain Plot##bf_show_cart", &show_cartesian_plot_);

    if (show_polar_plot_) {
        drawPolarPattern(res, look_azimuth_deg_, active_nulls_deg_);
    }

    if (show_cartesian_plot_) {
        drawCartesianPattern(res, look_azimuth_deg_, active_nulls_deg_);
    }

    if (bf_cfg.geometry == doa::ArrayGeometry::URA && res.is_2d) {
        draw2DHeatmap(res, look_azimuth_deg_, look_elevation_deg_);
    }
}

void BeamformerPanel::drawCartesianPattern(const beamforming::BeamformerResult& result, float look_az, const std::vector<float>& nulls) {
    if (result.pattern_azimuths_deg.empty() || result.pattern_gains_db.empty()) return;

    ImGui::Text("Spatial Response Beampattern (Azimuth vs Gain [dB]):");
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz = ImVec2(ImGui::GetContentRegionAvail().x, 150.0f);
    if (canvas_sz.x < 100.0f) canvas_sz.x = 100.0f;
    ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(18, 20, 26, 255));
    draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(50, 60, 80, 255));

    const float min_db = -60.0f;
    const float max_db = 5.0f;
    const float max_az = result.pattern_azimuths_deg.back();

    auto map_to_screen = [&](float az, float db) -> ImVec2 {
        float norm_x = std::clamp(az / max_az, 0.0f, 1.0f);
        float norm_y = std::clamp((db - min_db) / (max_db - min_db), 0.0f, 1.0f);
        return ImVec2(canvas_p0.x + norm_x * canvas_sz.x, canvas_p1.y - norm_y * canvas_sz.y);
    };

    // Horizontal dB grid lines
    float db_ticks[] = { 0.0f, -10.0f, -20.0f, -30.0f, -40.0f, -50.0f };
    for (float db : db_ticks) {
        ImVec2 p_l = map_to_screen(0.0f, db);
        ImVec2 p_r = map_to_screen(max_az, db);
        draw_list->AddLine(p_l, p_r, IM_COL32(40, 45, 60, 200), 1.0f);
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%+.0f dB", db);
        draw_list->AddText(ImVec2(canvas_p0.x + 4.0f, p_l.y - 12.0f), IM_COL32(100, 115, 135, 200), lbl);
    }

    // Beampattern curve
    size_t N = result.pattern_azimuths_deg.size();
    for (size_t i = 0; i < N - 1; ++i) {
        ImVec2 pt1 = map_to_screen(result.pattern_azimuths_deg[i], result.pattern_gains_db[i]);
        ImVec2 pt2 = map_to_screen(result.pattern_azimuths_deg[i + 1], result.pattern_gains_db[i + 1]);
        draw_list->AddLine(pt1, pt2, IM_COL32(0, 220, 255, 255), 2.0f);
    }

    // Look direction green marker
    ImVec2 p_look_top = map_to_screen(look_az, max_db);
    ImVec2 p_look_bot = map_to_screen(look_az, min_db);
    draw_list->AddLine(p_look_top, p_look_bot, IM_COL32(50, 255, 80, 220), 1.5f);
    draw_list->AddCircleFilled(map_to_screen(look_az, 0.0f), 4.5f, IM_COL32(50, 255, 80, 255));

    // Null direction red markers
    for (float null_az : nulls) {
        ImVec2 p_null_top = map_to_screen(null_az, max_db);
        ImVec2 p_null_bot = map_to_screen(null_az, min_db);
        draw_list->AddLine(p_null_top, p_null_bot, IM_COL32(255, 50, 50, 180), 1.0f);
        draw_list->AddTriangleFilled(
            ImVec2(p_null_top.x - 4.0f, p_null_top.y),
            ImVec2(p_null_top.x + 4.0f, p_null_top.y),
            ImVec2(p_null_top.x, p_null_top.y + 7.0f),
            IM_COL32(255, 50, 50, 255)
        );
    }

    ImGui::Dummy(canvas_sz);
}

void BeamformerPanel::drawPolarPattern(const beamforming::BeamformerResult& result, float look_az, const std::vector<float>& nulls) {
    if (result.pattern_azimuths_deg.empty() || result.pattern_gains_db.empty()) return;

    ImGui::Text("Polar Spatial Gain Response:");
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    float diameter = 220.0f;
    ImVec2 canvas_sz = ImVec2(diameter, diameter);
    ImVec2 center = ImVec2(canvas_p0.x + diameter * 0.5f, canvas_p0.y + diameter * 0.5f);
    float radius = diameter * 0.44f;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_p0, ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y), IM_COL32(15, 18, 24, 255));
    draw_list->AddRect(canvas_p0, ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y), IM_COL32(45, 55, 75, 255));

    // Concentric rings (-40 dB, -20 dB, 0 dB)
    draw_list->AddCircle(center, radius, IM_COL32(60, 75, 100, 255), 48, 1.5f);
    draw_list->AddCircle(center, radius * 0.66f, IM_COL32(40, 50, 70, 180), 36, 1.0f);
    draw_list->AddCircle(center, radius * 0.33f, IM_COL32(30, 40, 60, 150), 24, 1.0f);

    // Radial crosshairs
    draw_list->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), IM_COL32(40, 50, 70, 150));
    draw_list->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), IM_COL32(40, 50, 70, 150));

    // Polar response polygon
    const float min_db = -50.0f;
    const float max_db = 0.0f;
    size_t N = result.pattern_azimuths_deg.size();
    std::vector<ImVec2> poly_pts(N);

    for (size_t i = 0; i < N; ++i) {
        float az_rad = result.pattern_azimuths_deg[i] * DEG_TO_RAD;
        float db = std::clamp(result.pattern_gains_db[i], min_db, max_db);
        float norm_r = (db - min_db) / (max_db - min_db);
        float r = norm_r * radius;

        float x = center.x + r * std::sin(az_rad);
        float y = center.y - r * std::cos(az_rad);
        poly_pts[i] = ImVec2(x, y);
    }

    for (size_t i = 0; i < N - 1; ++i) {
        draw_list->AddLine(poly_pts[i], poly_pts[i + 1], IM_COL32(0, 240, 255, 230), 1.8f);
    }

    // Look vector (green line and arrowhead)
    float look_rad = look_az * DEG_TO_RAD;
    ImVec2 look_tip = ImVec2(center.x + (radius + 8.0f) * std::sin(look_rad), center.y - (radius + 8.0f) * std::cos(look_rad));
    draw_list->AddLine(center, look_tip, IM_COL32(50, 255, 80, 255), 2.0f);
    draw_list->AddCircleFilled(look_tip, 3.5f, IM_COL32(50, 255, 80, 255));

    // Null vectors (red lines)
    for (float null_az : nulls) {
        float null_rad = null_az * DEG_TO_RAD;
        ImVec2 null_tip = ImVec2(center.x + (radius + 6.0f) * std::sin(null_rad), center.y - (radius + 6.0f) * std::cos(null_rad));
        draw_list->AddLine(center, null_tip, IM_COL32(255, 50, 50, 220), 1.2f);
    }

    ImGui::Dummy(canvas_sz);
}

void BeamformerPanel::draw2DHeatmap(const beamforming::BeamformerResult& result, float look_az, float look_el) {
    if (result.pattern_2d_db.empty() || result.pattern_elevations_deg.empty()) return;

    ImGui::Spacing();
    ImGui::Text("2D URA Spatial Attenuation Map (Azimuth vs Elevation):");

    size_t num_el = result.pattern_elevations_deg.size();
    size_t num_az = result.pattern_2d_db.size() / num_el;

    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz = ImVec2(ImGui::GetContentRegionAvail().x, 140.0f);
    if (canvas_sz.x < 120.0f) canvas_sz.x = 120.0f;
    ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(10, 10, 15, 255));

    float cell_w = canvas_sz.x / static_cast<float>(num_az);
    float cell_h = canvas_sz.y / static_cast<float>(num_el);

    const float min_db = -50.0f;
    const float max_db = 0.0f;

    for (size_t e = 0; e < num_el; ++e) {
        float y0 = canvas_p1.y - static_cast<float>(e + 1) * cell_h;
        float y1 = y0 + cell_h;

        for (size_t a = 0; a < num_az; ++a) {
            float x0 = canvas_p0.x + static_cast<float>(a) * cell_w;
            float x1 = x0 + cell_w;

            float val = result.pattern_2d_db[e * num_az + a];
            float norm = std::clamp((val - min_db) / (max_db - min_db), 0.0f, 1.0f);
            ImU32 col = getTurboColor(norm);

            draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col);
        }
    }

    // Mark look direction with crosshair
    float look_norm_x = std::clamp(look_az / 360.0f, 0.0f, 1.0f);
    float look_norm_y = std::clamp(look_el / 90.0f, 0.0f, 1.0f);
    ImVec2 look_center = ImVec2(canvas_p0.x + look_norm_x * canvas_sz.x, canvas_p1.y - look_norm_y * canvas_sz.y);
    draw_list->AddCircle(look_center, 6.0f, IM_COL32(255, 255, 255, 255), 16, 2.0f);
    draw_list->AddLine(ImVec2(look_center.x - 9.0f, look_center.y), ImVec2(look_center.x + 9.0f, look_center.y), IM_COL32(255, 255, 255, 255), 1.5f);
    draw_list->AddLine(ImVec2(look_center.x, look_center.y - 9.0f), ImVec2(look_center.x, look_center.y + 9.0f), IM_COL32(255, 255, 255, 255), 1.5f);

    draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(60, 70, 90, 255));
    ImGui::Dummy(canvas_sz);
}

} // namespace rf_tools
