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

} // namespace rf_tools
