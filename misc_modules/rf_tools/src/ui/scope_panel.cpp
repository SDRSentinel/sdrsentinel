#include "scope_panel.h"
#include <imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace rf_tools {

static ImU32 getTurboCol(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
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

ScopePanel::ScopePanel() {}

void ScopePanel::loadPulse(const RawPulse& pulse, double sampleRate, double centerFreqHz) {
    if (frozen_) return;

    MOPResult res = MOPAnalyzer::analyze(pulse.samples, sampleRate, centerFreqHz);

    std::lock_guard<std::mutex> lock(mutex_);
    currentPulse_ = pulse;
    mopResult_ = std::move(res);
    sampleRate_ = sampleRate;
    centerFreqHz_ = centerFreqHz;
    pulseCounter_++;
    hasPulse_ = true;
    updateMatchedFilter();
}

void ScopePanel::draw() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Top control bar
    ImGui::Checkbox("Freeze / Hold Pulse##scope_freeze", &frozen_);
    ImGui::SameLine();
    if (ImGui::Button("Unfreeze & Catch Next##scope_unfreeze")) {
        frozen_ = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("| Captured Pulses: %llu", (unsigned long long)pulseCounter_);

    if (!hasPulse_ || mopResult_.envelope.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Awaiting pulse capture for intra-pulse modulation analysis...");
        return;
    }

    size_t numSamples = currentPulse_.samples.size();
    float durationUs = static_cast<float>(numSamples / sampleRate_ * 1.0e6);

    // Pulse metadata and MOP classification badge
    ImGui::Spacing();
    ImGui::Text("Pulse Duration: %.2f us (%zu samples) | Fs: %.2f MSPS", durationUs, numSamples, sampleRate_ / 1.0e6);

    // Color-coded MOP Badge
    switch (mopResult_.type) {
        case MOPType::LFM_CHIRP_UP:
            ImGui::TextColored(ImVec4(0.2f, 0.85f, 1.0f, 1.0f),
                "[MODULATION] LFM CHIRP (UP-RAMP) | Slope: %+.2f MHz/us | BW: %.2f MHz (R^2: %.2f)",
                mopResult_.chirpSlopeMhzPerUs, mopResult_.chirpBandwidthHz / 1.0e6f, mopResult_.chirpLinearityR2);
            break;
        case MOPType::LFM_CHIRP_DOWN:
            ImGui::TextColored(ImVec4(0.2f, 0.85f, 1.0f, 1.0f),
                "[MODULATION] LFM CHIRP (DOWN-RAMP) | Slope: %+.2f MHz/us | BW: %.2f MHz (R^2: %.2f)",
                mopResult_.chirpSlopeMhzPerUs, mopResult_.chirpBandwidthHz / 1.0e6f, mopResult_.chirpLinearityR2);
            break;
        case MOPType::PHASE_BARKER:
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                "[MODULATION] PHASE-CODED (Barker-%d) | Correlation: %.1f%%",
                mopResult_.barkerLength, mopResult_.barkerCorrelation * 100.0f);
            break;
        case MOPType::UNMODULATED_CW:
        default:
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f),
                "[MODULATION] UNMODULATED CW (Fixed Carrier Offset: %+.1f kHz)",
                mopResult_.meanFreqOffsetHz / 1.0e3f);
            break;
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Visualizer tabs
    if (ImGui::BeginTabBar("##scope_views")) {
        if (ImGui::BeginTabItem("Time Domain Waveform")) {
            drawWaveformPlot();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Instantaneous Frequency / Chirp")) {
            drawInstFreqPlot();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Chirp Matched Filter (Pulse Compression)")) {
            drawMatchedFilterPlot();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("I/Q Constellation Orbit")) {
            drawConstellationPlot();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("STFT Spectrogram")) {
            drawSTFTSpectrogram();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

void ScopePanel::drawWaveformPlot() {
    float width = ImGui::GetContentRegionAvail().x;
    float height = 180.0f;

    ImGui::Checkbox("Envelope##scope_env", &showEnvelope_);
    ImGui::SameLine();
    ImGui::Checkbox("In-Phase (I)##scope_i", &showI_);
    ImGui::SameLine();
    ImGui::Checkbox("Quadrature (Q)##scope_q", &showQ_);

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(12, 16, 20, 255));
    draw_list->AddRect(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(40, 55, 70, 255));

    // Midline
    float midY = cursor.y + height * 0.5f;
    draw_list->AddLine(ImVec2(cursor.x, midY), ImVec2(cursor.x + width, midY), IM_COL32(40, 60, 80, 160), 1.0f);

    size_t N = mopResult_.envelope.size();
    if (N < 2) return;

    // Find peak amplitude for vertical autoscaling
    float peakAmp = 0.001f;
    for (float v : mopResult_.envelope) {
        if (v > peakAmp) peakAmp = v;
    }
    float scaleY = (height * 0.45f) / peakAmp;

    size_t step = std::max((size_t)1, N / static_cast<size_t>(width));

    auto plotSeries = [&](const std::vector<float>& series, ImU32 col, bool isEnvelope) {
        ImVec2 p_prev;
        for (size_t i = 0; i < N; i += step) {
            float x = cursor.x + (static_cast<float>(i) / static_cast<float>(N - 1)) * width;
            float y = isEnvelope ? (cursor.y + height - series[i] * scaleY - 8.0f)
                                 : (midY - series[i] * scaleY);
            ImVec2 p_curr(x, y);
            if (i > 0) {
                draw_list->AddLine(p_prev, p_curr, col, 1.5f);
            }
            p_prev = p_curr;
        }
    };

    if (showI_) plotSeries(mopResult_.iSamples, IM_COL32(80, 200, 255, 200), false);
    if (showQ_) plotSeries(mopResult_.qSamples, IM_COL32(255, 160, 60, 200), false);
    if (showEnvelope_) plotSeries(mopResult_.envelope, IM_COL32(50, 255, 100, 240), true);

    ImGui::Dummy(ImVec2(width, height + 8.0f));
}

void ScopePanel::drawInstFreqPlot() {
    float width = ImGui::GetContentRegionAvail().x;
    float height = 180.0f;

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(10, 14, 18, 255));
    draw_list->AddRect(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(40, 55, 70, 255));

    size_t N = mopResult_.instFreqHz.size();
    if (N < 2) return;

    // Find min and max instantaneous frequency
    float minF = mopResult_.instFreqHz[0];
    float maxF = mopResult_.instFreqHz[0];
    for (float f : mopResult_.instFreqHz) {
        if (f < minF) minF = f;
        if (f > maxF) maxF = f;
    }
    float spanF = std::max(1000.0f, maxF - minF);

    // Center grid line
    float zeroY = cursor.y + height * (1.0f - (0.0f - minF) / spanF);
    if (zeroY >= cursor.y && zeroY <= cursor.y + height) {
        draw_list->AddLine(ImVec2(cursor.x, zeroY), ImVec2(cursor.x + width, zeroY), IM_COL32(60, 70, 80, 160), 1.0f);
    }

    size_t step = std::max((size_t)1, N / static_cast<size_t>(width));
    ImVec2 p_prev;
    for (size_t i = 0; i < N; i += step) {
        float x = cursor.x + (static_cast<float>(i) / static_cast<float>(N - 1)) * width;
        float normF = std::clamp((mopResult_.instFreqHz[i] - minF) / spanF, 0.0f, 1.0f);
        float y = cursor.y + height * (1.0f - normF);
        ImVec2 p_curr(x, y);
        if (i > 0) {
            draw_list->AddLine(p_prev, p_curr, IM_COL32(70, 180, 255, 240), 2.0f);
        }
        p_prev = p_curr;
    }

    // Min / Max labels
    char minLabel[32], maxLabel[32];
    std::snprintf(minLabel, sizeof(minLabel), "%.1f kHz", minF / 1.0e3f);
    std::snprintf(maxLabel, sizeof(maxLabel), "%.1f kHz", maxF / 1.0e3f);
    draw_list->AddText(ImVec2(cursor.x + 6.0f, cursor.y + 4.0f), IM_COL32(160, 180, 200, 200), maxLabel);
    draw_list->AddText(ImVec2(cursor.x + 6.0f, cursor.y + height - 18.0f), IM_COL32(160, 180, 200, 200), minLabel);

    ImGui::Dummy(ImVec2(width, height + 8.0f));
}

void ScopePanel::drawConstellationPlot() {
    float availWidth = ImGui::GetContentRegionAvail().x;
    float radius = std::clamp(availWidth * 0.22f, 90.0f, 140.0f);

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 center = ImVec2(cursor.x + radius + 15.0f, cursor.y + radius + 15.0f);

    // Dark circular phosphor CRT background
    draw_list->AddCircleFilled(center, radius, IM_COL32(10, 18, 14, 255), 48);
    draw_list->AddCircle(center, radius, IM_COL32(35, 70, 45, 255), 48, 2.0f);
    draw_list->AddCircle(center, radius * 0.5f, IM_COL32(20, 50, 30, 140), 32, 1.0f);

    // Crosshair axes (I and Q)
    draw_list->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), IM_COL32(30, 60, 40, 160), 1.0f);
    draw_list->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), IM_COL32(30, 60, 40, 160), 1.0f);

    draw_list->AddText(ImVec2(center.x + radius - 15.0f, center.y + 4.0f), IM_COL32(70, 150, 90, 200), "+I");
    draw_list->AddText(ImVec2(center.x + 4.0f, center.y - radius + 2.0f), IM_COL32(70, 150, 90, 200), "+Q");

    size_t N = mopResult_.iSamples.size();
    if (N < 2) return;

    // Peak amplitude for normalization
    float peakAmp = 0.001f;
    for (float v : mopResult_.envelope) {
        if (v > peakAmp) peakAmp = v;
    }

    ImVec2 p_prev;
    for (size_t i = 0; i < N; ++i) {
        float normI = (mopResult_.iSamples[i] / peakAmp) * (radius * 0.88f);
        float normQ = (mopResult_.qSamples[i] / peakAmp) * (radius * 0.88f);

        ImVec2 p_curr = ImVec2(center.x + normI, center.y - normQ);

        float progress = static_cast<float>(i) / static_cast<float>(N);
        int alpha = static_cast<int>(80.0f + 175.0f * progress);

        if (i > 0) {
            draw_list->AddLine(p_prev, p_curr, IM_COL32(40, 220, 90, alpha), 1.2f);
        }
        draw_list->AddCircleFilled(p_curr, 1.5f, IM_COL32(180, 255, 200, alpha), 8);
        p_prev = p_curr;
    }

    ImGui::Dummy(ImVec2(radius * 2.0f + 30.0f, radius * 2.0f + 30.0f));
}

void ScopePanel::drawSTFTSpectrogram() {
    const auto& spec = mopResult_.spectrogram;
    if (spec.numTimeBins == 0 || spec.numFreqBins == 0) {
        ImGui::TextDisabled("Pulse too short for STFT spectrogram decomposition");
        return;
    }

    float width = ImGui::GetContentRegionAvail().x;
    float height = 180.0f;

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(10, 14, 18, 255));

    float cellW = width / static_cast<float>(spec.numTimeBins);
    float cellH = height / static_cast<float>(spec.numFreqBins);
    float spanDb = std::max(10.0f, spec.maxDb - spec.minDb);

    for (size_t t = 0; t < spec.numTimeBins; ++t) {
        float x0 = cursor.x + static_cast<float>(t) * cellW;
        float x1 = x0 + cellW;

        for (size_t f = 0; f < spec.numFreqBins; ++f) {
            // Plot frequency vertically with positive frequencies on top
            float y0 = cursor.y + static_cast<float>(spec.numFreqBins - 1 - f) * cellH;
            float y1 = y0 + cellH;

            float db = spec.dataDb[t * spec.numFreqBins + f];
            float norm = std::clamp((db - spec.minDb) / spanDb, 0.0f, 1.0f);

            draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), getTurboCol(norm));
        }
    }

    draw_list->AddRect(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(40, 55, 70, 255));

    char fTop[32], fBot[32];
    std::snprintf(fTop, sizeof(fTop), "+%.1f MHz", (sampleRate_ * 0.5) / 1.0e6);
    std::snprintf(fBot, sizeof(fBot), "-%.1f MHz", (sampleRate_ * 0.5) / 1.0e6);
    draw_list->AddText(ImVec2(cursor.x + 4.0f, cursor.y + 4.0f), IM_COL32(240, 240, 240, 220), fTop);
    draw_list->AddText(ImVec2(cursor.x + 4.0f, cursor.y + height - 18.0f), IM_COL32(240, 240, 240, 220), fBot);

    ImGui::Dummy(ImVec2(width, height + 8.0f));
}

void ScopePanel::updateMatchedFilter() {
    if (currentPulse_.samples.size() < 16) {
        compressionResult_ = mop::ChirpCompressionResult{};
        return;
    }

    double slopeHzPerSec = 0.0;
    if (mopResult_.type == MOPType::LFM_CHIRP_UP || mopResult_.type == MOPType::LFM_CHIRP_DOWN) {
        slopeHzPerSec = static_cast<double>(mopResult_.chirpSlopeMhzPerUs) * 1.0e12; // MHz/us -> Hz/s
    } else if (forceMatchedFilter_ && std::abs(manualChirpRateMhzPerUs_) > 0.001f) {
        slopeHzPerSec = static_cast<double>(manualChirpRateMhzPerUs_) * 1.0e12;
    } else {
        compressionResult_ = mop::ChirpCompressionResult{};
        return;
    }

    mop::ChirpMatchedFilter mf;
    mf.init(sampleRate_);
    std::vector<std::complex<float>> cplx(currentPulse_.samples.size());
    for (size_t i = 0; i < currentPulse_.samples.size(); ++i) {
        cplx[i] = { currentPulse_.samples[i].re, currentPulse_.samples[i].im };
    }

    mop::ChirpMatchedFilter::WindowType win = mop::ChirpMatchedFilter::WindowType::RECTANGULAR;
    if (selectedWindow_ == 1) win = mop::ChirpMatchedFilter::WindowType::HAMMING;
    else if (selectedWindow_ == 2) win = mop::ChirpMatchedFilter::WindowType::HANN;

    compressionResult_ = mf.compressPulse(cplx, slopeHzPerSec, win);
}

void ScopePanel::drawMatchedFilterPlot() {
    float width = ImGui::GetContentRegionAvail().x;
    float height = 200.0f;

    // Control header
    ImGui::Text("Sidelobe Window:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170.0f);
    const char* winItems[] = { "Rectangular (Uniform)", "Hamming (Radar Std)", "Hann (Cosine^2)" };
    if (ImGui::Combo("##mf_win_select", &selectedWindow_, winItems, IM_ARRAYSIZE(winItems))) {
        updateMatchedFilter();
    }

    ImGui::SameLine();
    ImGui::Checkbox("dB Scale##mf_db", &dbScale_);
    ImGui::SameLine();
    ImGui::Checkbox("Zoom Peak (+-35 samps)##mf_zoom", &zoomPeak_);

    ImGui::SameLine();
    if (ImGui::Checkbox("Manual / Force Slope##mf_force", &forceMatchedFilter_)) {
        updateMatchedFilter();
    }

    if (forceMatchedFilter_) {
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::SliderFloat("Slope (MHz/us)##mf_slope_slider", &manualChirpRateMhzPerUs_, -10.0f, 10.0f, "%+.2f MHz/us")) {
            updateMatchedFilter();
        }
    }

    if (!compressionResult_.detected || compressionResult_.compressedEnvelope.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
            "No LFM chirp detected in current pulse. Check 'Manual / Force Slope' above to test pulse compression.");
        return;
    }

    // Results info badges
    const char* winNames[] = { "Rectangular (No Sidelobe Taper)", "Hamming (-40dB Sidelobe Suppression)", "Hann" };
    const char* winName = (selectedWindow_ >= 0 && selectedWindow_ < 3) ? winNames[selectedWindow_] : "Custom";
    ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f),
        "[COMPRESSION] Ratio: %.1fx (%.2f us -> %.2f us) | SNR Gain: +%.1f dB | BW: %.2f MHz",
        compressionResult_.compressionRatio,
        compressionResult_.uncompressedPulseWidthSec * 1.0e6,
        compressionResult_.compressedPulseWidthSec * 1.0e6,
        compressionResult_.peakCompressedSnrGainDb,
        compressionResult_.chirpBandwidthHz / 1.0e6);

    ImVec4 pslrCol = (compressionResult_.pslrDb <= -25.0f) ? ImVec4(0.3f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
    ImGui::TextColored(pslrCol,
        "[SIDELOBES] Peak-to-Sidelobe Ratio (PSLR): %.1f dB | Active Window: %s",
        compressionResult_.pslrDb, winName);

    // Canvas drawing
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(10, 14, 20, 255));
    draw_list->AddRect(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(40, 55, 75, 255));

    const auto& env = compressionResult_.compressedEnvelope;
    size_t envSize = env.size();
    if (envSize < 2) return;

    // Find peak index
    size_t peakIdx = 0;
    float peakVal = env[0];
    for (size_t i = 1; i < envSize; ++i) {
        if (env[i] > peakVal) {
            peakVal = env[i];
            peakIdx = i;
        }
    }

    size_t startIdx = 0;
    size_t endIdx = envSize - 1;
    if (zoomPeak_) {
        startIdx = (peakIdx > 35) ? (peakIdx - 35) : 0;
        endIdx = std::min(envSize - 1, peakIdx + 35);
    }
    size_t plotSpan = std::max((size_t)1, endIdx - startIdx);

    auto mapCoord = [&](size_t idx, float normVal, float dbVal) -> ImVec2 {
        float x = cursor.x + (static_cast<float>(idx - startIdx) / static_cast<float>(plotSpan)) * width;
        float y;
        if (dbScale_) {
            // Map 0 dB to top, -50 dB to bottom
            float clampedDb = std::clamp(dbVal, -50.0f, 0.0f);
            y = cursor.y + (-clampedDb / 50.0f) * height;
        } else {
            // Map 1.0 to top, 0.0 to bottom
            float clampedNorm = std::clamp(normVal, 0.0f, 1.05f);
            y = cursor.y + (1.0f - clampedNorm / 1.05f) * height;
        }
        return ImVec2(x, y);
    };

    // Grid lines
    if (dbScale_) {
        static const float gridDbs[] = { 0.0f, -3.0f, -13.2f, -20.0f, -30.0f, -40.0f };
        for (float db : gridDbs) {
            float y = cursor.y + (-db / 50.0f) * height;
            ImU32 lineCol = (db == -3.0f) ? IM_COL32(240, 180, 50, 180) :
                            (db == -13.2f) ? IM_COL32(220, 80, 80, 120) :
                                             IM_COL32(40, 55, 75, 120);
            draw_list->AddLine(ImVec2(cursor.x, y), ImVec2(cursor.x + width, y), lineCol, 1.0f);
            char dbBuf[16];
            std::snprintf(dbBuf, sizeof(dbBuf), "%.1f dB", db);
            draw_list->AddText(ImVec2(cursor.x + 4.0f, y - 13.0f), IM_COL32(140, 160, 180, 200), dbBuf);
        }
    } else {
        static const float gridNorms[] = { 1.0f, 0.707f, 0.5f, 0.25f, 0.0f };
        for (float n : gridNorms) {
            float y = cursor.y + (1.0f - n / 1.05f) * height;
            ImU32 lineCol = (n == 0.707f) ? IM_COL32(240, 180, 50, 180) : IM_COL32(40, 55, 75, 120);
            draw_list->AddLine(ImVec2(cursor.x, y), ImVec2(cursor.x + width, y), lineCol, 1.0f);
            char nBuf[16];
            std::snprintf(nBuf, sizeof(nBuf), (n == 0.707f) ? "-3dB (0.707)" : "%.2f", n);
            draw_list->AddText(ImVec2(cursor.x + 4.0f, y - 13.0f), IM_COL32(140, 160, 180, 200), nBuf);
        }
    }

    // Draw compressed pulse envelope curve
    ImVec2 pPrev;
    for (size_t i = startIdx; i <= endIdx; ++i) {
        float valNorm = env[i];
        float valDb = 20.0f * std::log10(std::max(valNorm, 1e-4f));
        ImVec2 pCurr = mapCoord(i, valNorm, valDb);

        if (i > startIdx) {
            // Fill translucent area under curve
            ImVec2 pBase0(pPrev.x, cursor.y + height);
            ImVec2 pBase1(pCurr.x, cursor.y + height);
            draw_list->AddQuadFilled(pPrev, pCurr, pBase1, pBase0, IM_COL32(0, 180, 220, 30));
            // Curve stroke
            draw_list->AddLine(pPrev, pCurr, IM_COL32(0, 220, 255, 255), 2.0f);
        }
        pPrev = pCurr;
    }

    // Highlight peak marker
    if (peakIdx >= startIdx && peakIdx <= endIdx) {
        ImVec2 peakPt = mapCoord(peakIdx, env[peakIdx], 0.0f);
        draw_list->AddCircleFilled(peakPt, 4.0f, IM_COL32(255, 255, 255, 255));
        draw_list->AddLine(ImVec2(peakPt.x, cursor.y), ImVec2(peakPt.x, cursor.y + height), IM_COL32(0, 220, 255, 100), 1.0f);
    }

    ImGui::Dummy(ImVec2(width, height + 8.0f));
}

} // namespace rf_tools
