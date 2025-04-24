#pragma once
#include "../doa/types.h"
#include "../doa/music_engine.h"
#include "../doa/covariance_accumulator.h"
#include <vector>
#include <mutex>

namespace rf_tools {

class DOAPanel {
public:
    DOAPanel();
    ~DOAPanel() = default;

    void updateResult(const doa::DOAResult& result);
    void draw(doa::MUSICEngine& engine, doa::CovarianceAccumulator& accumulator);

private:
    void drawPolarCompass(const std::vector<float>& angles, float radius);
    void drawSpectrumPlot(const std::vector<float>& angles, const std::vector<float>& spectrum_db);

    doa::DOAConfig config_;
    doa::DOAResult latest_result_;
    mutable std::mutex mutex_;

    // UI state
    int ui_geometry_idx_ = 0; // 0: ULA, 1: UCA
    int ui_num_elements_ = 4;
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
