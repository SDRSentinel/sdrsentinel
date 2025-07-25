#pragma once
#include <functional>
#include <memory>
#include <string>
#include "../vna/vna_analyzer.h"
#include "../vna/scalar_sweep.h"

namespace rf_tools {

class VNAPanel {
public:
    // Receiver access for the scalar sweep, supplied by the module
    struct Host {
        std::function<bool()> sourceRunning;
        // Creates the probe (on the calling UI thread); returns nullptr and sets err on failure
        std::function<std::unique_ptr<vna::ScalarProbe>(const vna::ScalarSweepSettings&, std::string& err)> makeProbe;
    };

    VNAPanel();
    ~VNAPanel() = default;

    void setHost(const Host& host) { host_ = host; }
    void draw(vna::VNAAnalyzer& analyzer);

private:
    void drawImport(vna::VNAAnalyzer& analyzer);
    void drawScalarSweep(vna::VNAAnalyzer& analyzer);
    void drawMagnitudePlot(const std::vector<vna::VNASweepPoint>& trace, const vna::ResonatorDipFit& fit, bool validated);
    void drawPhasePlot(const std::vector<vna::VNASweepPoint>& trace);
    vna::ScalarSweepSettings sweepSettings() const;
    void startSweep(vna::SweepStage stage);

    Host host_;
    vna::ScalarSweep sweep_;

    float ui_start_mhz_ = 98.0f;
    float ui_stop_mhz_ = 102.0f;
    int ui_points_ = 201;
    bool ui_find_dips_ = true;

    float ui_rbw_khz_ = 10.0f;
    float ui_offset_khz_ = 250.0f;
    float ui_settle_ms_ = 50.0f;
    float ui_dwell_ms_ = 20.0f;

    char import_path_[512] = "";
    char export_path_[512] = "resonator_trace.csv";
    std::string import_error_;
    std::string export_msg_;
    bool export_msg_error_ = false;
    std::string sweep_msg_;
    bool sweep_msg_error_ = false;
};

} // namespace rf_tools
