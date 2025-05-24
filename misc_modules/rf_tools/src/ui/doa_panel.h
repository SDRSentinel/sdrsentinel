#pragma once
#include "../doa/types.h"
#include "../doa/music_engine.h"
#include "../doa/covariance_accumulator.h"
#include <vector>
#include <deque>
#include <mutex>
#include <chrono>

namespace rf_tools {

struct PhosphorBlip {
    float angleDeg = 0.0f;
    float powerDbfs = -40.0f;
    float intensity = 1.0f;
    std::chrono::steady_clock::time_point timestamp;
};

struct WaterfallRow {
    std::vector<float> spectrumDb;
    std::vector<float> anglesDeg;
    std::chrono::steady_clock::time_point timestamp;
};

enum class DOADisplayMode {
    ALL,
    POLAR_COMPASS,
    PPI_RADAR,
    WATERFALL
};

class DOAPanel {
public:
    DOAPanel();
    ~DOAPanel() = default;

    void updateResult(const doa::DOAResult& result);
    void addPulseHit(float angleDeg, float powerDbfs);
    void draw(doa::MUSICEngine& engine, doa::CovarianceAccumulator& accumulator);

    doa::DOAResult getLastResult() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_result_;
    }
    const doa::DOAConfig& getConfig() const { return config_; }

private:
    void drawPolarCompass(const std::vector<float>& angles, float radius);
    void drawRadarPPI(const std::vector<float>& angles, float radius);
    void drawAzimuthWaterfall(float width, float height);
    void drawSpectrumPlot(const std::vector<float>& angles, const std::vector<float>& spectrum_db);
    void drawAzimuthElevationHeatmap(float width, float height, const doa::DOAResult& res);

    doa::DOAConfig config_;
    doa::DOAResult latest_result_;
    mutable std::mutex mutex_;

    // Phosphor persistence & PPI radar state
    std::deque<PhosphorBlip> phosphor_blips_;
    float phosphor_decay_sec_ = 2.5f;
    bool sweep_enabled_ = true;
    float sweep_rpm_ = 24.0f;
    float current_sweep_angle_ = 0.0f;
    std::chrono::steady_clock::time_point last_sweep_time_;

    // Azimuth waterfall state
    std::deque<WaterfallRow> waterfall_history_;
    size_t max_waterfall_rows_ = 80;
    int waterfall_palette_ = 0; // 0: Phosphor Green, 1: Amber Radar, 2: Turbo
    float waterfall_min_db_ = -35.0f;
    float waterfall_max_db_ = 0.0f;

    // UI state
    DOADisplayMode display_mode_ = DOADisplayMode::ALL;
    int ui_display_mode_idx_ = 0;
    int ui_geometry_idx_ = 0; // 0: ULA, 1: UCA, 2: URA (2D)
    int ui_num_elements_ = 4;
    int ui_elements_x_ = 2;
    int ui_elements_y_ = 2;
    float ui_spacing_ = 0.5f;
    int ui_snapshots_ = 256;
    float ui_forgetting_ = 0.0f;
    bool ui_enable_fbss_ = false;
    bool ui_auto_sources_ = true;
    int ui_expected_sources_ = 1;
    float ui_pilot_angle_ = 90.0f;

    bool cal_active_ = false;
    std::vector<std::complex<float>> current_cal_factors_;
};

} // namespace rf_tools
