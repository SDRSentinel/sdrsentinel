#pragma once
#include "../vna/vna_analyzer.h"

namespace rf_tools {

class VNAPanel {
public:
    VNAPanel();
    ~VNAPanel() = default;

    void draw(vna::VNAAnalyzer& analyzer);

private:
    void drawMagnitudePlot(const std::vector<vna::VNASweepPoint>& trace, const vna::ResonatorDipFit& fit);
    void drawPhasePlot(const std::vector<vna::VNASweepPoint>& trace);

    float ui_start_mhz_ = 98.0f;
    float ui_stop_mhz_ = 102.0f;
    int ui_points_ = 201;
    bool ui_find_dips_ = true;
    float ui_synth_q_ = 2500.0f;
    float ui_synth_depth_ = 25.0f;
};

} // namespace rf_tools
