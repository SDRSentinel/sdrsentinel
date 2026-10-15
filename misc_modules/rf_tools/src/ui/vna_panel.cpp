#include "vna_panel.h"
#include "../vna/trace_import.h"
#include <gui/gui.h>
#include <gui/style.h>
#include <imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

namespace rf_tools {

namespace {

std::string formatTime(std::chrono::system_clock::time_point tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return buf;
}

std::string baseName(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return (p == std::string::npos) ? path : path.substr(p + 1);
}

const ImVec4 kErrorColor(1.0f, 0.4f, 0.3f, 1.0f);

bool hasPhase(const std::vector<vna::VNASweepPoint>& trace) {
    for (const auto& pt : trace) {
        if (std::isfinite(pt.phaseDeg)) { return true; }
    }
    return false;
}

} // namespace

VNAPanel::VNAPanel() {}

vna::ScalarSweepSettings VNAPanel::sweepSettings() const {
    vna::ScalarSweepSettings s;
    s.startHz = ui_start_mhz_ * 1e6;
    s.stopHz = ui_stop_mhz_ * 1e6;
    s.points = ui_points_;
    s.rbwHz = ui_rbw_khz_ * 1e3;
    s.offsetHz = ui_offset_khz_ * 1e3;
    s.settleSec = ui_settle_ms_ * 1e-3;
    s.dwellSec = ui_dwell_ms_ * 1e-3;
    return s;
}

void VNAPanel::draw(vna::VNAAnalyzer& analyzer) {
    auto trace = analyzer.getTrace();
    auto fit = analyzer.getDipFit();
    auto cfg = analyzer.getConfig();

    // Collect a finished sweep (the sweep thread never touches the analyzer)
    vna::ScalarSweepResult result;
    if (sweep_.pollFinished(result)) {
        sweep_msg_error_ = !result.ok;
        if (!result.ok) {
            sweep_msg_ = result.error;
        }
        else if (result.stage == vna::SweepStage::Thru) {
            sweep_msg_ = "Thru reference stored";
        }
        else if (result.stage == vna::SweepStage::Noise) {
            sweep_msg_ = "Noise floor stored";
        }
        else {
            analyzer.setFullTrace(result.trace);
            analyzer.setTraceSource("Scalar |S21| sweep " + formatTime(result.finishedAt) + ", " + vna::ScalarSweep::describe(result.settings)
                                    + (sweep_.hasNoise() ? ", noise floor applied" : ", no noise floor"), vna::TraceOrigin::ScalarSweep);
            sweep_msg_ = "DUT sweep done";
            trace = analyzer.getTrace();
            fit = analyzer.getDipFit();
        }
    }
    if (!sweep_.isRunning()) { sweep_.invalidateIfChanged(sweepSettings()); }

    float availWidth = ImGui::GetContentRegionAvail().x;

    // Controls
    if (ImGui::CollapsingHeader("Sweep range & resonator setup##vna_ctrl_hdr", ImGuiTreeNodeFlags_DefaultOpen)) {
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
            analyzer.setFullTrace(analyzer.getTrace()); // refit the loaded trace in the new mode
            trace = analyzer.getTrace();
            fit = analyzer.getDipFit();
        }
        ImGui::TextDisabled("Start, stop and points are used by the scalar sweep below.");
    }

    drawImport(analyzer);
    drawScalarSweep(analyzer);

    // Results Badge
    ImGui::Spacing();
    std::string source = analyzer.getTraceSource();
    if (trace.empty()) {
        ImGui::TextDisabled("No trace: load a Touchstone/CSV file or run a sweep");
    }
    else {
        ImGui::TextWrapped("Source: %s", source.c_str());
        size_t unreliable = 0;
        float minDr = INFINITY;
        for (const auto& pt : trace) {
            if (!pt.valid) { unreliable++; }
            if (std::isfinite(pt.dynRangeDb)) { minDr = std::min(minDr, pt.dynRangeDb); }
        }
        if (std::isfinite(minDr)) {
            ImGui::Text("Dynamic range (thru - floor): min %.1f dB", minDr);
        }
        if (unreliable > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%zu of %zu points within %.0f dB of the noise floor: unreliable, excluded from the fit",
                               unreliable, trace.size(), (double)vna::ScalarSweep::kUnreliableMarginDb);
        }
        if (fit.valid) {
            vna::FitValidation check = vna::validateFit(trace, fit, analyzer.getTraceOrigin());
            const ImVec4 dim(1.0f, 0.65f, 0.2f, 1.0f);
            ImGui::TextColored(check.validated ? ImVec4(0.2f, 0.9f, 1.0f, 1.0f) : dim,
                "%s f0 = %.4f MHz | BW(-3dB) = %.1f kHz | Dip = %.1f dB",
                check.validated ? "[RESONANCE]" : "[NOT VALIDATED]",
                fit.resonantFreqHz / 1e6, fit.bandwidth3dbHz / 1e3, fit.dipDepthDb);
            ImGui::TextColored(check.validated ? ImVec4(0.3f, 1.0f, 0.4f, 1.0f) : dim,
                "Loaded Q (Q_L): %.0f | Coupling Q (Q_c): %.0f | Internal Q (Q_i): %.0f",
                fit.loadedQ, fit.couplingQ, fit.internalQ);
            ImGui::PushTextWrapPos(0.0f);
            if (check.validated) {
                ImGui::TextDisabled("Validated: %s", check.reason.c_str());
            }
            else {
                ImGui::TextColored(dim, "Not validated: %s. Do not use these values.", check.reason.c_str());
            }
            ImGui::PopTextWrapPos();
        }
        else {
            ImGui::TextDisabled("No resonance fit (needs at least 7 usable points and a 1.5 dB dip or peak)");
        }

        ImGui::SetNextItemWidth(availWidth * 0.5f);
        ImGui::InputText("##vna_export_path", export_path_, sizeof(export_path_));
        ImGui::SameLine();
        if (ImGui::Button("Export CSV##vna_export")) {
            std::string err = vna::writeTraceCsv(export_path_, trace);
            export_msg_error_ = !err.empty();
            export_msg_ = err.empty() ? std::string("Wrote ") + export_path_ : err;
        }
        if (!export_msg_.empty()) {
            ImGui::TextColored(export_msg_error_ ? kErrorColor : ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "%s", export_msg_.c_str());
        }
    }
    ImGui::Separator();

    // Plot tabs
    if (ImGui::BeginTabBar("##vna_plot_tabs")) {
        if (ImGui::BeginTabItem("S21 Transmission Magnitude (|S21|)")) {
            drawMagnitudePlot(trace, fit, vna::validateFit(trace, fit, analyzer.getTraceOrigin()).validated);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("S21 Transmission Phase (deg)")) {
            if (trace.empty()) {
                ImGui::TextDisabled("No trace");
            }
            else if (!hasPhase(trace)) {
                ImGui::TextDisabled("Phase not measured (scalar sweep)");
            }
            else {
                drawPhasePlot(trace);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void VNAPanel::drawImport(vna::VNAAnalyzer& analyzer) {
    if (!ImGui::CollapsingHeader("Import from a VNA file (.s2p / .csv)##vna_imp_hdr", ImGuiTreeNodeFlags_DefaultOpen)) { return; }

    ImGui::TextWrapped("Touchstone .s2p (S21 is read) or CSV with columns freq_hz, s21_db[, phase_deg].");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
    ImGui::InputText("##vna_import_path", import_path_, sizeof(import_path_));
    ImGui::SameLine();
    if (ImGui::Button("Load##vna_import_load")) {
        auto res = vna::loadTraceFile(import_path_);
        if (res.ok) {
            analyzer.setFullTrace(res.trace);
            analyzer.setTraceSource("File " + baseName(import_path_) + " (" + res.format + "), loaded " + formatTime(std::chrono::system_clock::now())
                                    + ", " + std::to_string(res.trace.size()) + " points", vna::TraceOrigin::File);
            import_error_.clear();
        }
        else {
            import_error_ = res.error;
        }
    }
    if (!import_error_.empty()) {
        ImGui::TextColored(kErrorColor, "%s", import_error_.c_str());
    }
}

void VNAPanel::startSweep(vna::SweepStage stage) {
    sweep_msg_.clear();
    sweep_msg_error_ = true;
    auto s = sweepSettings();
    std::string err = sweep_.checkPrerequisites(stage, s);
    if (err.empty() && !host_.makeProbe) { err = "No receiver available"; }
    std::unique_ptr<vna::ScalarProbe> probe;
    if (err.empty()) { probe = host_.makeProbe(s, err); }
    if (err.empty()) { err = sweep_.start(stage, s, std::move(probe)); }
    sweep_msg_ = err;
    sweep_msg_error_ = !err.empty();
}

void VNAPanel::drawScalarSweep(vna::VNAAnalyzer& analyzer) {
    (void)analyzer;
    if (!ImGui::CollapsingHeader("Scalar |S21| sweep (SDRSentinel receiver + external source)##vna_sw_hdr")) { return; }

    ImGui::TextWrapped("Needs a broadband source (noise source or tracking generator) feeding the DUT and the receiver. "
                       "Receiver gain must not change between the thru and DUT sweeps, and AGC must be off. "
                       "Magnitude only; the sweep is slow (settle + dwell per point).");

    bool running = sweep_.isRunning();
    auto s = sweepSettings();

    ImGui::BeginDisabled(running);
    float w = ImGui::GetContentRegionAvail().x * 0.5f;
    ImGui::LeftLabel("RBW (kHz)");
    ImGui::SetNextItemWidth(w);
    ImGui::InputFloat("##vna_rbw", &ui_rbw_khz_, 1.0f, 10.0f, "%.1f");
    ImGui::LeftLabel("VFO offset (kHz)");
    ImGui::SetNextItemWidth(w);
    ImGui::InputFloat("##vna_off", &ui_offset_khz_, 50.0f, 100.0f, "%.1f");
    ImGui::LeftLabel("Settle (ms)");
    ImGui::SetNextItemWidth(w);
    ImGui::InputFloat("##vna_settle", &ui_settle_ms_, 5.0f, 20.0f, "%.0f");
    ImGui::LeftLabel("Dwell (ms)");
    ImGui::SetNextItemWidth(w);
    ImGui::InputFloat("##vna_dwell", &ui_dwell_ms_, 5.0f, 20.0f, "%.0f");
    ImGui::EndDisabled();

    std::string settingsErr = vna::ScalarSweep::validate(s);
    ImGui::Text("Estimated duration: %.1f s (plus retune overhead)", vna::ScalarSweep::estimateSeconds(s));
    if (!settingsErr.empty()) { ImGui::TextColored(kErrorColor, "%s", settingsErr.c_str()); }

    bool srcRunning = host_.sourceRunning && host_.sourceRunning();
    if (!srcRunning) { ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "SDRSentinel source is not running: start it to sweep"); }

    if (sweep_.hasThru()) {
        ImGui::Text("Thru reference: %s", formatTime(sweep_.thruTime()).c_str());
    }
    else {
        ImGui::TextDisabled("Thru reference: none (invalidated when start, stop, points, RBW or offset change)");
    }
    if (sweep_.hasNoise()) {
        ImGui::Text("Noise floor: %s", formatTime(sweep_.noiseTime()).c_str());
    }
    else {
        ImGui::TextDisabled("Noise floor: none (optional)");
    }

    ImGui::BeginDisabled(running || !srcRunning);
    if (ImGui::Button("1. Measure thru##vna_thru")) { startSweep(vna::SweepStage::Thru); }
    ImGui::SameLine();
    ImGui::BeginDisabled(!sweep_.hasThru());
    if (ImGui::Button("2. Measure DUT##vna_dut")) { startSweep(vna::SweepStage::Dut); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("3. Measure noise floor##vna_noise")) { startSweep(vna::SweepStage::Noise); }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Source off, same cable and setup as the thru"); }

    if (running) {
        ImGui::ProgressBar(sweep_.progress(), ImVec2(ImGui::GetContentRegionAvail().x * 0.6f, 0.0f));
        ImGui::SameLine();
        if (ImGui::Button("Cancel##vna_cancel")) { sweep_.cancel(); }
    }
    if (!sweep_msg_.empty()) {
        ImGui::TextColored(sweep_msg_error_ ? kErrorColor : ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "%s", sweep_msg_.c_str());
    }
}

void VNAPanel::drawMagnitudePlot(const std::vector<vna::VNASweepPoint>& trace, const vna::ResonatorDipFit& fit, bool validated) {
    float width = ImGui::GetContentRegionAvail().x;
    float height = 220.0f;

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(12, 16, 22, 255));
    draw_list->AddRect(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(45, 60, 80, 255));

    if (trace.size() < 2) {
        draw_list->AddText(ImVec2(cursor.x + 8, cursor.y + 8), IM_COL32(120, 150, 180, 220), "No trace: load a Touchstone/CSV file or run a sweep");
        ImGui::Dummy(ImVec2(width, height + 6.0f));
        return;
    }

    // Y scale follows the data
    float minDb = INFINITY, maxDb = -INFINITY;
    for (const auto& pt : trace) {
        minDb = std::min(minDb, pt.magnitudeDb);
        maxDb = std::max(maxDb, pt.magnitudeDb);
    }
    minDb = std::floor((minDb - 3.0f) / 10.0f) * 10.0f;
    maxDb = std::ceil((maxDb + 2.0f) / 5.0f) * 5.0f;

    auto mapCoord = [&](size_t idx, float db) -> ImVec2 {
        float x = cursor.x + (static_cast<float>(idx) / static_cast<float>(trace.size() - 1)) * width;
        float y = cursor.y + (1.0f - (db - minDb) / (maxDb - minDb + 1e-6f)) * height;
        return ImVec2(x, y);
    };

    // Grid lines every 10 dB
    for (float db = std::ceil(minDb / 10.0f) * 10.0f; db <= maxDb; db += 10.0f) {
        float y = cursor.y + (1.0f - (db - minDb) / (maxDb - minDb + 1e-6f)) * height;
        draw_list->AddLine(ImVec2(cursor.x, y), ImVec2(cursor.x + width, y), IM_COL32(40, 50, 65, 120));
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%+.0f dB", db);
        draw_list->AddText(ImVec2(cursor.x + 4, y - 14), IM_COL32(120, 150, 180, 200), buf);
    }

    // Plot S21 curve; unreliable points are marked
    ImVec2 pPrev;
    for (size_t i = 0; i < trace.size(); ++i) {
        ImVec2 pCurr = mapCoord(i, trace[i].magnitudeDb);
        if (i > 0) {
            draw_list->AddLine(pPrev, pCurr, IM_COL32(50, 180, 255, 255), 2.0f);
        }
        if (!trace[i].valid) {
            draw_list->AddCircleFilled(pCurr, 3.0f, IM_COL32(255, 170, 50, 255));
        }
        pPrev = pCurr;
    }

    // Highlight resonance dip
    if (fit.valid) {
        double fMin = trace.front().freqHz, fMax = trace.back().freqHz;
        float xDip = cursor.x + static_cast<float>((fit.resonantFreqHz - fMin) / (fMax - fMin + 1e-9)) * width;
        ImU32 markerCol = validated ? IM_COL32(255, 80, 80, 180) : IM_COL32(255, 165, 50, 110);
        draw_list->AddLine(ImVec2(xDip, cursor.y), ImVec2(xDip, cursor.y + height), markerCol, 1.5f);

        char markerBuf[64];
        std::snprintf(markerBuf, sizeof(markerBuf), validated ? "f0: %.3f MHz" : "f0: %.3f MHz (not validated)", fit.resonantFreqHz / 1e6);
        draw_list->AddText(ImVec2(xDip + 6, cursor.y + height - 20), validated ? IM_COL32(255, 100, 100, 255) : IM_COL32(255, 165, 50, 220), markerBuf);
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
    bool prevOk = false;
    for (size_t i = 0; i < trace.size(); ++i) {
        bool ok = std::isfinite(trace[i].phaseDeg);
        ImVec2 pCurr = ok ? mapCoord(i, trace[i].phaseDeg) : ImVec2(0, 0);
        if (ok && prevOk) {
            draw_list->AddLine(pPrev, pCurr, IM_COL32(255, 170, 50, 240), 1.8f);
        }
        pPrev = pCurr;
        prevOk = ok;
    }

    ImGui::Dummy(ImVec2(width, height + 6.0f));
}

} // namespace rf_tools
