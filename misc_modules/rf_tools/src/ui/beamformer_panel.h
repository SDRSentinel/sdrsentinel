#pragma once
#include <imgui.h>
#include <vector>
#include "beamforming/beamformer.h"
#include "doa/covariance_accumulator.h"
#include "doa/music_engine.h"
#include "ui/doa_panel.h"
#include "pri/emitter_tracker.h"

namespace rf_tools {

class BeamformerPanel {
public:
    BeamformerPanel();
    ~BeamformerPanel() = default;

    void draw(beamforming::AdaptiveBeamformer& beamformer,
              doa::CovarianceAccumulator& accumulator,
              const DOAPanel& doaPanel,
              const EmitterTracker& emitterTracker);

    bool isFilterAudioEnabled() const { return filter_audio_enabled_; }

private:
    void drawCartesianPattern(const beamforming::BeamformerResult& result, float look_az, const std::vector<float>& nulls);
    void drawPolarPattern(const beamforming::BeamformerResult& result, float look_az, const std::vector<float>& nulls);
    void draw2DHeatmap(const beamforming::BeamformerResult& result, float look_az, float look_el);

    int selected_algo_idx_{ 1 }; // 0: Bartlett, 1: MVDR, 2: Robust LSMI, 3: LCMV
    float look_azimuth_deg_{ 90.0f };
    float look_elevation_deg_{ 0.0f };
    float diag_loading_factor_{ 1e-3f };
    bool auto_track_target_{ false };
    int tracked_emitter_id_{ -1 };

    // Null management
    std::vector<float> active_nulls_deg_;
    float new_null_deg_{ 45.0f };

    // Display options
    bool show_polar_plot_{ true };
    bool show_cartesian_plot_{ true };
    bool filter_audio_enabled_{ false };

    // Turbo colormap helper for 2D
    static ImU32 getTurboColor(float t);
};

} // namespace rf_tools
