#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <core.h>
#include <config.h>
#include <utils/flog.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include "pipeline.h"

SDRPP_MOD_INFO{
    /* Name:            */ "kraken_doa",
    /* Description:     */ "Direction finding with a KrakenSDR (Heimdall DAQ client)",
    /* Author:          */ "Suleyman Poyraz",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

ConfigManager config;

using kraken_doa::DoaPipeline;
using kraken_doa::DoaSettings;
using kraken_doa::PipelineStatus;
using kraken_doa::SkipKind;

namespace {
    const ImVec4 COL_OK(0.2f, 1.0f, 0.2f, 1.0f);
    const ImVec4 COL_WARN(1.0f, 0.8f, 0.1f, 1.0f);
    const ImVec4 COL_ERR(1.0f, 0.3f, 0.3f, 1.0f);
    const ImVec4 COL_INFO(0.6f, 0.8f, 1.0f, 1.0f);

    const float SPECTRUM_RANGE_DB = 40.0f;

    std::string clockString(std::chrono::system_clock::time_point tp) {
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm tmv;
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmv);
        return buf;
    }
}

class KrakenDoaModule : public ModuleManager::Instance {
public:
    KrakenDoaModule(std::string name) {
        this->name = name;
        loadConfig();
        settings.offsetHz = offsetKHz * 1e3;
        settings.bandwidthHz = bandwidthKHz * 1e3;
        pipe.setSettings(settings);
        gui::menu.registerEntry(name, menuHandler, this, this);
        flog::info("KrakenSDR DOA instance '{0}' created", name);
    }

    ~KrakenDoaModule() {
        gui::menu.removeEntry(name);
        pipe.stop();
        pipe.disconnectControl();
        flog::info("KrakenSDR DOA instance '{0}' destroyed", name);
    }

    void postInit() override {}

    void enable() override { enabled = true; }

    void disable() override {
        pipe.stop();
        if (controlWanted) {
            controlWanted = false;
            pipe.disconnectControl();
        }
        enabled = false;
    }

    bool isEnabled() override { return enabled; }

private:
    // ------------------------------------------------------------ config
    void loadConfig() {
        config.acquire();
        json& c = config.conf[name];
        auto get = [&](const char* key, auto& v) {
            if (c.contains(key)) {
                try { v = c[key].get<std::remove_reference_t<decltype(v)>>(); } catch (...) {}
            }
        };
        get("host", host);
        get("dataPort", dataPort);
        get("controlPort", controlPort);
        get("offsetKHz", offsetKHz);
        get("bandwidthKHz", bandwidthKHz);
        get("squelchDbfs", settings.squelchDbfs);
        int v;
        v = 0; get("arrayType", v); settings.arrayType = (v == 1) ? kraken_doa::dsp::ArrayType::ULA : kraken_doa::dsp::ArrayType::UCA;
        get("elements", settings.elements);
        get("sizeMeters", settings.sizeMeters);
        get("arrayOffsetDeg", settings.arrayOffsetDeg);
        get("compassOffsetDeg", settings.compassOffsetDeg);
        v = 0; get("half", v); settings.half = (v == 1) ? kraken_doa::dsp::ScanHalf::Forward : (v == 2) ? kraken_doa::dsp::ScanHalf::Backward : kraken_doa::dsp::ScanHalf::Both;
        v = 2; get("algorithm", v); settings.algorithm = (v == 0) ? kraken_doa::dsp::DoaAlgorithm::Bartlett : (v == 1) ? kraken_doa::dsp::DoaAlgorithm::Capon : kraken_doa::dsp::DoaAlgorithm::Music;
        get("numSources", settings.numSources);
        get("freqMHz", freqMHz);
        config.release();
        snprintf(hostBuf, sizeof(hostBuf), "%s", host.c_str());
        settings.elements = std::clamp(settings.elements, 2, 32);
        settings.numSources = std::clamp(settings.numSources, 1, std::max(1, settings.elements - 1));
        dataPort = std::clamp(dataPort, 1, 65535);
        controlPort = std::clamp(controlPort, 1, 65535);
        bandwidthKHz = std::max(0.1, bandwidthKHz);
        settings.sizeMeters = std::max(0.001, settings.sizeMeters);
        if (freqMHz > 0) { freqTouched = true; }
    }

    void saveConfig() {
        config.acquire();
        json& c = config.conf[name];
        c["host"] = host;
        c["dataPort"] = dataPort;
        c["controlPort"] = controlPort;
        c["offsetKHz"] = offsetKHz;
        c["bandwidthKHz"] = bandwidthKHz;
        c["squelchDbfs"] = settings.squelchDbfs;
        c["arrayType"] = settings.arrayType == kraken_doa::dsp::ArrayType::ULA ? 1 : 0;
        c["elements"] = settings.elements;
        c["sizeMeters"] = settings.sizeMeters;
        c["arrayOffsetDeg"] = settings.arrayOffsetDeg;
        c["compassOffsetDeg"] = settings.compassOffsetDeg;
        c["half"] = (int)settings.half == (int)kraken_doa::dsp::ScanHalf::Forward ? 1 : (int)settings.half == (int)kraken_doa::dsp::ScanHalf::Backward ? 2 : 0;
        c["algorithm"] = settings.algorithm == kraken_doa::dsp::DoaAlgorithm::Bartlett ? 0 : settings.algorithm == kraken_doa::dsp::DoaAlgorithm::Capon ? 1 : 2;
        c["numSources"] = settings.numSources;
        if (freqTouched) { c["freqMHz"] = freqMHz; }
        config.release(true);
    }

    void settingsChanged() {
        settings.offsetHz = offsetKHz * 1e3;
        settings.bandwidthHz = bandwidthKHz * 1e3;
        pipe.setSettings(settings);
        saveConfig();
    }

    // ------------------------------------------------------------ UI
    static void menuHandler(void* ctx) {
        KrakenDoaModule* _this = (KrakenDoaModule*)ctx;
        _this->drawMenu();
    }

    void drawMenu() {
        const float menuWidth = ImGui::GetContentRegionAvail().x;
        PipelineStatus st = pipe.status();
        const auto cs = st.iq.state;
        const bool streaming = cs == kraken_doa::heimdall::ConnState::Streaming;
        const bool idle = cs == kraken_doa::heimdall::ConnState::Disconnected || cs == kraken_doa::heimdall::ConnState::Error;

        drawConnection(st, menuWidth, idle);
        if (streaming || st.iq.haveLastHeader) { drawDaqStatus(st); }
        drawSignal(st, menuWidth);
        drawArray(st, menuWidth);
        drawBearing(st, menuWidth);
    }

    void drawConnection(const PipelineStatus& st, float menuWidth, bool idle) {
        using kraken_doa::heimdall::ConnState;
        ImGui::SeparatorText("Connection");
        if (!idle) { ImGui::BeginDisabled(); }
        ImGui::LeftLabel("Host");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputText(("##kraken_host_" + name).c_str(), hostBuf, sizeof(hostBuf))) {
            host = hostBuf;
            saveConfig();
        }
        ImGui::LeftLabel("Data port");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt(("##kraken_dport_" + name).c_str(), &dataPort, 0, 0)) {
            dataPort = std::clamp(dataPort, 1, 65535);
            saveConfig();
        }
        if (!idle) { ImGui::EndDisabled(); }

        if (idle) {
            if (ImGui::Button(("Connect##kraken_connect_" + name).c_str(), ImVec2(menuWidth, 0))) {
                host = hostBuf;
                settingsChanged();
                flog::info("KrakenSDR DOA: connecting to {0}:{1}", host, dataPort);
                pipe.start(host, dataPort);
            }
        }
        else {
            if (ImGui::Button(("Disconnect##kraken_connect_" + name).c_str(), ImVec2(menuWidth, 0))) {
                flog::info("KrakenSDR DOA: disconnecting");
                pipe.stop();
            }
        }
        ImGui::PushTextWrapPos(0.0f);
        switch (st.iq.state) {
        case ConnState::Disconnected: ImGui::TextDisabled("Not connected"); break;
        case ConnState::Connecting: ImGui::TextColored(COL_WARN, "Connecting to %s:%d", host.c_str(), dataPort); break;
        case ConnState::AwaitingFrame: ImGui::TextColored(COL_WARN, "Connected to %s:%d, waiting for the first Heimdall frame", host.c_str(), dataPort); break;
        case ConnState::Streaming: ImGui::TextColored(COL_OK, "Receiving Heimdall frames"); break;
        case ConnState::Error: ImGui::TextColored(COL_ERR, "Connection error: %s", st.iq.errorMessage.c_str()); break;
        }
        ImGui::PopTextWrapPos();

        // Optional DAQ control
        if (ImGui::Checkbox(("Control the DAQ##kraken_ctl_" + name).c_str(), &controlWanted)) {
            host = hostBuf;
            if (controlWanted) { pipe.connectControl(host, controlPort); }
            else { pipe.disconnectControl(); }
        }
        if (controlWanted || st.control.connected) {
            if (st.control.connected) { ImGui::BeginDisabled(); }
            ImGui::LeftLabel("Control port");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::InputInt(("##kraken_cport_" + name).c_str(), &controlPort, 0, 0)) {
                controlPort = std::clamp(controlPort, 1, 65535);
                saveConfig();
            }
            if (st.control.connected) { ImGui::EndDisabled(); }
            drawControl(st, menuWidth);
        }
        else {
            ImGui::TextDisabled("Control is off: frequency and gain are set on the DAQ itself.");
        }
    }

    void drawControl(const PipelineStatus& st, float menuWidth) {
        const auto& c = st.control;
        if (c.busy) { ImGui::TextColored(COL_WARN, "Control command in progress..."); }
        else if (c.connected) { ImGui::TextColored(COL_OK, "Control connected"); }
        else if (!c.lastError.empty()) { ImGui::TextColored(COL_ERR, "Control not connected"); }
        if (!c.lastError.empty()) { ImGui::TextColored(COL_ERR, "Control error: %s", c.lastError.c_str()); }
        else if (!c.lastInfo.empty()) { ImGui::TextDisabled("%s", c.lastInfo.c_str()); }

        if (!c.connected || c.busy) { ImGui::BeginDisabled(); }

        if (!freqTouched && st.iq.haveLastHeader) { freqMHz = st.iq.lastHeader.rfCenterFreq / 1e6; }
        ImGui::LeftLabel("Frequency (MHz)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble(("##kraken_freq_" + name).c_str(), &freqMHz, 0.0, 0.0, "%.6f")) {
            freqTouched = true;
            freqMHz = std::max(freqMHz, 0.0);
            saveConfig();
        }
        if (ImGui::Button(("Apply frequency##kraken_freq_apply_" + name).c_str(), ImVec2(menuWidth, 0))) {
            freqTouched = true;
            pipe.requestCenterFrequency((uint64_t)std::llround(freqMHz * 1e6));
        }

        // Gains are in dB, applied to all channels; the channel count comes from the last header
        static const std::vector<float> gains = kraken_doa::heimdall::ControlClient::acceptedGainsDb();
        const bool haveHdr = st.iq.haveLastHeader;
        if (!haveHdr) { ImGui::BeginDisabled(); }
        char preview[48];
        if (gainIdx >= 0) { snprintf(preview, sizeof(preview), "%.1f dB", gains[gainIdx]); }
        else if (haveHdr) { snprintf(preview, sizeof(preview), "now %.1f dB (ch 0)", st.iq.lastHeader.ifGains[0] / 10.0f); }
        else { snprintf(preview, sizeof(preview), "waiting for a frame"); }
        ImGui::LeftLabel("Gain");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::BeginCombo(("##kraken_gain_" + name).c_str(), preview)) {
            for (size_t i = 0; i < gains.size(); i++) {
                char item[24];
                snprintf(item, sizeof(item), "%.1f dB", gains[i]);
                if (ImGui::Selectable(item, (int)i == gainIdx)) {
                    gainIdx = (int)i;
                    pipe.requestGain(gains[i], st.iq.lastHeader.activeAntChs);
                }
            }
            ImGui::EndCombo();
        }
        if (!haveHdr) { ImGui::EndDisabled(); }
        if (ImGui::Button(("AGC##kraken_agc_" + name).c_str(), ImVec2(menuWidth, 0))) {
            gainIdx = -1;
            pipe.requestAgc();
        }
        if (!c.connected || c.busy) { ImGui::EndDisabled(); }

        if (c.awaitingResync) {
            ImGui::TextColored(COL_WARN, "DAQ is recalibrating after the frequency change (requested %.6f MHz); no bearing until the sync flags return.",
                               c.requestedFreqHz / 1e6);
        }
    }

    void drawDaqStatus(const PipelineStatus& st) {
        const auto& h = st.iq.lastHeader;
        ImGui::SeparatorText("DAQ status");
        if (!st.iq.haveLastHeader) {
            ImGui::TextDisabled("No frame received yet");
            return;
        }
        ImGui::Text("Hardware: %s   channels: %u", h.hardwareId, h.activeAntChs);
        ImGui::Text("fc %.6f MHz   fs %.3f MS/s   CPI %u samples", h.rfCenterFreq / 1e6, h.samplingFreq / 1e6, h.cpiLength);
        const bool synced = h.delaySyncFlag && h.iqSyncFlag;
        ImGui::Text("Sync state: %s (%u)", kraken_doa::syncStateName(h.syncState), h.syncState);
        ImGui::TextColored(h.delaySyncFlag ? COL_OK : COL_WARN, "delay sync %u", h.delaySyncFlag);
        ImGui::SameLine();
        ImGui::TextColored(h.iqSyncFlag ? COL_OK : COL_WARN, "IQ sync %u", h.iqSyncFlag);
        ImGui::SameLine();
        ImGui::TextColored(synced ? COL_OK : COL_WARN, synced ? "(synchronized)" : "(not usable)");
        ImGui::Text("Noise source: %s   last frame: %s", h.noiseSourceState ? "ON" : "off", kraken_doa::frameTypeName(h.frameType));

        ImGui::Text("ADC overdrive:");
        for (uint32_t c = 0; c < h.activeAntChs && c < 32; c++) {
            ImGui::SameLine();
            bool od = (h.adcOverdriveFlags >> c) & 1u;
            ImGui::TextColored(od ? COL_ERR : COL_OK, "%u%s", c, od ? "!" : "");
        }
        std::string g = "IF gains (dB):";
        for (uint32_t c = 0; c < h.activeAntChs && c < 32; c++) {
            char b[16];
            snprintf(b, sizeof(b), " %.1f", h.ifGains[c] / 10.0f);
            g += b;
        }
        ImGui::TextUnformatted(g.c_str());

        const auto& s = st.iq;
        ImGui::Text("Frames: %llu received, %llu usable", (unsigned long long)s.framesReceived, (unsigned long long)s.framesUsable);
        ImGui::Text("DATA %llu  CAL %llu  DUMMY %llu  other %llu", (unsigned long long)s.framesByType[kraken_doa::heimdall::FRAME_DATA],
                    (unsigned long long)s.framesByType[kraken_doa::heimdall::FRAME_CAL],
                    (unsigned long long)s.framesByType[kraken_doa::heimdall::FRAME_DUMMY],
                    (unsigned long long)(s.framesByType[2] + s.framesByType[4]));
        ImGui::Text("CPI gaps: %llu   overwritten: %llu", (unsigned long long)s.cpiGaps, (unsigned long long)s.framesOverwritten);
        ImGui::Text("Throughput: %.1f Mbit/s   processing: %s", s.bytesPerSecond * 8.0 / 1e6,
                    st.processingMs > 0 ? (std::to_string((int)std::lround(st.processingMs)) + " ms/frame").c_str() : "no frame processed");
        if (s.cpiGaps > 0) {
            ImGui::TextColored(COL_WARN, "The DAQ dropped %llu frame(s) (cpi_index jumps). The link or the DAQ host cannot keep up: use wired gigabit Ethernet or raise the DAQ decimation.", (unsigned long long)s.cpiGaps);
        }
        if (s.framesOverwritten > 0) {
            ImGui::TextColored(COL_WARN, "%llu frame(s) arrived faster than they were processed; only the newest frame is used.", (unsigned long long)s.framesOverwritten);
        }
    }

    void drawSignal(const PipelineStatus& st, float menuWidth) {
        ImGui::SeparatorText("Signal");
        ImGui::LeftLabel("Offset (kHz)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble(("##kraken_off_" + name).c_str(), &offsetKHz, 1.0, 10.0, "%.3f")) { settingsChanged(); }
        ImGui::LeftLabel("Bandwidth (kHz)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble(("##kraken_bw_" + name).c_str(), &bandwidthKHz, 1.0, 10.0, "%.3f")) {
            bandwidthKHz = std::max(0.1, bandwidthKHz);
            settingsChanged();
        }
        float sq = (float)settings.squelchDbfs;
        ImGui::LeftLabel("Squelch (dBFS)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderFloat(("##kraken_sq_" + name).c_str(), &sq, -120.0f, 0.0f, "%.1f")) {
            settings.squelchDbfs = sq;
            settingsChanged();
        }
        if (st.havePower) {
            const bool below = st.channelPowerDbfs < settings.squelchDbfs;
            ImGui::TextColored(below ? COL_WARN : COL_OK, "Channel power: %.1f dBFS%s", st.channelPowerDbfs, below ? " (below squelch)" : "");
        }
        else {
            ImGui::TextDisabled("Channel power: no usable frame yet");
        }
    }

    void drawArray(const PipelineStatus& st, float menuWidth) {
        using namespace kraken_doa::dsp;
        ImGui::SeparatorText("Array");
        bool ch = false;
        int type = settings.arrayType == ArrayType::ULA ? 1 : 0;
        ImGui::LeftLabel("Type");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::Combo(("##kraken_atype_" + name).c_str(), &type, "UCA (circular)\0ULA (linear)\0")) {
            settings.arrayType = type == 1 ? ArrayType::ULA : ArrayType::UCA;
            ch = true;
        }
        ImGui::LeftLabel("Elements");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt(("##kraken_elems_" + name).c_str(), &settings.elements, 1, 1)) {
            settings.elements = std::clamp(settings.elements, 2, 32);
            settings.numSources = std::clamp(settings.numSources, 1, settings.elements - 1);
            ch = true;
        }
        ImGui::LeftLabel(type == 1 ? "Spacing (m)" : "Radius (m)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble(("##kraken_size_" + name).c_str(), &settings.sizeMeters, 0.01, 0.1, "%.3f")) {
            settings.sizeMeters = std::max(0.001, settings.sizeMeters);
            ch = true;
        }
        ImGui::LeftLabel("Array offset (deg)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble(("##kraken_aoff_" + name).c_str(), &settings.arrayOffsetDeg, 1.0, 10.0, "%.1f")) { ch = true; }
        ImGui::LeftLabel("Compass offset (deg)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble(("##kraken_coff_" + name).c_str(), &settings.compassOffsetDeg, 1.0, 10.0, "%.1f")) { ch = true; }

        if (type == 1) {
            int half = settings.half == ScanHalf::Forward ? 1 : settings.half == ScanHalf::Backward ? 2 : 0;
            ImGui::LeftLabel("ULA half");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::Combo(("##kraken_half_" + name).c_str(), &half, "Both\0Forward\0Backward\0")) {
                settings.half = half == 1 ? ScanHalf::Forward : half == 2 ? ScanHalf::Backward : ScanHalf::Both;
                ch = true;
            }
            ImGui::TextWrapped("A linear array cannot tell front from back: a source and its mirror image give the same result.");
        }
        else {
            settings.half = ScanHalf::Both;
        }

        int alg = settings.algorithm == DoaAlgorithm::Bartlett ? 0 : settings.algorithm == DoaAlgorithm::Capon ? 1 : 2;
        ImGui::LeftLabel("Algorithm");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::Combo(("##kraken_alg_" + name).c_str(), &alg, "Bartlett\0Capon\0MUSIC\0")) {
            settings.algorithm = alg == 0 ? DoaAlgorithm::Bartlett : alg == 1 ? DoaAlgorithm::Capon : DoaAlgorithm::Music;
            ch = true;
        }
        if (settings.algorithm == DoaAlgorithm::Music) {
            ImGui::LeftLabel("Sources");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::InputInt(("##kraken_nsrc_" + name).c_str(), &settings.numSources, 1, 1)) {
                settings.numSources = std::clamp(settings.numSources, 1, settings.elements - 1);
                ch = true;
            }
        }
        if (ch) { settingsChanged(); }

        // Geometry in wavelengths at the signal frequency
        if (st.iq.haveLastHeader) {
            const double f = (double)st.iq.lastHeader.rfCenterFreq + offsetKHz * 1e3;
            if (f > 0) {
                const double lambda = SPEED_OF_LIGHT / f;
                const double wl = settings.sizeMeters / lambda;
                ImGui::Text("Wavelength %.3f m at %.4f MHz", lambda, f / 1e6);
                ImGui::Text("%s %.3f m = %.3f wavelengths", type == 1 ? "Spacing" : "Radius", settings.sizeMeters, wl);
                double spacing = settings.sizeMeters / lambda;
                if (type == 0) { spacing = 2.0 * wl * std::sin(M_PI / settings.elements); }
                if (type == 0) { ImGui::Text("Neighbour spacing %.3f wavelengths", spacing); }
                if (spacing > 0.5) {
                    ImGui::TextColored(COL_WARN, "%s spacing %.2f wavelengths exceeds 0.5: spatial aliasing, the bearing can be ambiguous.",
                                       type == 1 ? "ULA" : "UCA", spacing);
                }
            }
        }
        else {
            ImGui::TextDisabled("Size in wavelengths is shown once a frame gives the frequency.");
        }
    }

    // ------------------------------------------------------------ bearing
    // Returns an empty string when a fresh result may be drawn, else the status line text and colour.
    std::string statusLine(const PipelineStatus& st, bool& fresh, ImVec4& col) const {
        using kraken_doa::heimdall::ConnState;
        fresh = false;
        col = COL_WARN;
        switch (st.iq.state) {
        case ConnState::Disconnected: col = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled); return "Not connected";
        case ConnState::Connecting: return "Connecting";
        case ConnState::Error: col = COL_ERR; return "Connection error: " + st.iq.errorMessage;
        case ConnState::AwaitingFrame: return "Waiting for the first Heimdall frame";
        case ConnState::Streaming: break;
        }
        if (!st.haveSkip && !st.haveResult) { return "Waiting for first frame"; }

        double period = std::max(5.0, 3.0 * st.framePeriodSec);
        double age = st.haveResult ? std::chrono::duration<double>(st.now - st.last.receivedAt).count() : 0.0;
        if (st.haveResult && age <= period) {
            fresh = true;
            return "";
        }
        std::string reason;
        switch (st.skipKind) {
        case SkipKind::NotSynced: {
            const auto& h = st.iq.lastHeader;
            char b[160];
            snprintf(b, sizeof(b), "DAQ calibrating (state %s, delay_sync %u, iq_sync %u)", kraken_doa::syncStateName(h.syncState), h.delaySyncFlag, h.iqSyncFlag);
            reason = b;
            break;
        }
        case SkipKind::NotDataFrame: reason = "Frame not usable (" + st.skipReason + ")"; break;
        case SkipKind::ChannelMismatch: reason = "Channel count mismatch: " + st.skipReason; break;
        case SkipKind::BelowSquelch: reason = "Below squelch: " + st.skipReason; break;
        case SkipKind::None: reason = "No new result"; break;
        default: reason = st.skipReason; break;
        }
        if (st.haveResult) {
            char b[96];
            snprintf(b, sizeof(b), "Last bearing is stale (%.0f s old). ", age);
            reason = b + reason;
        }
        return reason;
    }

    void drawBearing(const PipelineStatus& st, float menuWidth) {
        ImGui::SeparatorText("Bearing");
        bool fresh;
        ImVec4 col;
        std::string line = statusLine(st, fresh, col);
        if (!fresh) {
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextWrapped("%s", line.c_str());
            ImGui::PopStyleColor();
        }
        else {
            drawResult(st, menuWidth);
        }
        drawHistory(st);
    }

    static ImVec2 polar(ImVec2 c, float r, double bearingDeg) {
        const double a = bearingDeg * M_PI / 180.0;
        return ImVec2(c.x + r * (float)std::sin(a), c.y - r * (float)std::cos(a));
    }

    void drawResult(const PipelineStatus& st, float menuWidth) {
        const auto& r = st.last;
        const auto& d = r.doa;
        const bool full = d.thetaDeg.size() >= 360;

        // Latest frame note when it did not produce a result (the plot is the last real one)
        if (st.skipKind != SkipKind::None && !st.skipReason.empty()) {
            ImGui::TextColored(COL_WARN, "Latest frame: %s", st.skipReason.c_str());
        }

        ImGui::TextColored(COL_OK, "Bearing %.1f deg", d.bearingDeg);
        ImGui::Text("Peak quality %.1f dB (peak minus median)", d.peakQualityDb);
        const double age = std::chrono::duration<double>(st.now - r.receivedAt).count();
        ImGui::Text("CPI %u   age %.1f s   power %.1f dBFS", r.cpiIndex, age, r.powerDbfs);
        ImGui::Text("DAQ time stamp %llu", (unsigned long long)r.headerTimeStamp);
        const char* an = r.algorithm == kraken_doa::dsp::DoaAlgorithm::Bartlett ? "Bartlett" : r.algorithm == kraken_doa::dsp::DoaAlgorithm::Capon ? "Capon" : "MUSIC";
        ImGui::Text("%s at %.4f MHz, fs %.3f MS/s", an, r.signalFreqHz / 1e6, r.sampleRateHz / 1e6);
        if (r.algorithm == kraken_doa::dsp::DoaAlgorithm::Music && !d.eigenvalues.empty()) {
            std::string e = "Eigenvalues:";
            for (double v : d.eigenvalues) {
                char b[24];
                snprintf(b, sizeof(b), " %.3g", v);
                e += b;
            }
            ImGui::TextWrapped("%s", e.c_str());
        }

        // Compass plot
        const float side = std::min(menuWidth, 280.0f);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(menuWidth, side));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c(p0.x + menuWidth * 0.5f, p0.y + side * 0.5f);
        const float R = side * 0.5f - 16.0f;
        const ImU32 gridCol = IM_COL32(150, 150, 150, 110);
        const ImU32 textCol = IM_COL32(220, 220, 220, 255);
        dl->AddCircle(c, R, gridCol, 64);
        dl->AddCircle(c, R * 0.25f, gridCol, 32);
        dl->AddCircle(c, R * 0.625f, gridCol, 48);
        for (int b = 0; b < 360; b += 30) { dl->AddLine(polar(c, R * 0.25f, b), polar(c, R, b), gridCol); }
        static const char* card[4] = { "N", "E", "S", "W" };
        for (int i = 0; i < 4; i++) {
            ImVec2 tp = polar(c, R + 9.0f, i * 90.0);
            ImVec2 ts = ImGui::CalcTextSize(card[i]);
            dl->AddText(ImVec2(tp.x - ts.x * 0.5f, tp.y - ts.y * 0.5f), textCol, card[i]);
        }
        // Spectrum, radius from -SPECTRUM_RANGE_DB (inner ring) to 0 dB (rim)
        std::vector<ImVec2> pts;
        pts.reserve(d.thetaDeg.size());
        for (size_t i = 0; i < d.thetaDeg.size(); i++) {
            const double b = kraken_doa::dsp::bearingFromTheta(d.thetaDeg[i], r.compassOffsetDeg);
            const float v = std::clamp((d.spectrumDb[i] + SPECTRUM_RANGE_DB) / SPECTRUM_RANGE_DB, 0.0f, 1.0f);
            pts.push_back(polar(c, R * (0.25f + 0.75f * v), b));
        }
        if (pts.size() >= 2) {
            // bearing runs opposite to theta, drawing order does not matter for a closed or open polyline
            dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(80, 200, 255, 255), full ? ImDrawFlags_Closed : ImDrawFlags_None, 1.5f);
        }
        // Needle
        const ImVec2 tip = polar(c, R, d.bearingDeg);
        dl->AddLine(c, tip, IM_COL32(255, 80, 80, 255), 2.5f);
        dl->AddCircleFilled(tip, 4.0f, IM_COL32(255, 80, 80, 255));
        ImGui::TextDisabled("Compass plot: 0 deg up, clockwise. Spectrum: rim = peak, inner ring = -%.0f dB.", SPECTRUM_RANGE_DB);

        // Spectrum vs bearing
        const float ph = 110.0f;
        ImVec2 q0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(menuWidth, ph + 14.0f));
        const float L = q0.x + 30.0f, Rr = q0.x + menuWidth - 6.0f, T = q0.y + 4.0f, B = q0.y + ph;
        dl->AddRect(ImVec2(L, T), ImVec2(Rr, B), gridCol);
        for (int db = 0; db >= -(int)SPECTRUM_RANGE_DB; db -= 10) {
            const float y = T + (B - T) * (float)(-db) / SPECTRUM_RANGE_DB;
            dl->AddLine(ImVec2(L, y), ImVec2(Rr, y), gridCol);
            char t[8];
            snprintf(t, sizeof(t), "%d", db);
            dl->AddText(ImVec2(q0.x, y - 7.0f), textCol, t);
        }
        for (int b = 0; b <= 360; b += 90) {
            const float x = L + (Rr - L) * b / 360.0f;
            dl->AddLine(ImVec2(x, T), ImVec2(x, B), gridCol);
            char t[8];
            snprintf(t, sizeof(t), "%d", b);
            dl->AddText(ImVec2(x - 8.0f, B + 1.0f), textCol, t);
        }
        std::vector<std::pair<double, float>> sp;
        sp.reserve(d.thetaDeg.size());
        for (size_t i = 0; i < d.thetaDeg.size(); i++) {
            sp.emplace_back(kraken_doa::dsp::bearingFromTheta(d.thetaDeg[i], r.compassOffsetDeg), d.spectrumDb[i]);
        }
        std::sort(sp.begin(), sp.end());
        const double step = d.thetaDeg.size() > 1 ? std::fabs(d.thetaDeg[1] - d.thetaDeg[0]) : 1.0;
        for (size_t i = 1; i < sp.size(); i++) {
            if (sp[i].first - sp[i - 1].first > 2.0 * step + 1e-6) { continue; }
            auto mp = [&](const std::pair<double, float>& s) {
                const float v = std::clamp(-s.second / SPECTRUM_RANGE_DB, 0.0f, 1.0f);
                return ImVec2(L + (Rr - L) * (float)(s.first / 360.0), T + (B - T) * v);
            };
            dl->AddLine(mp(sp[i - 1]), mp(sp[i]), IM_COL32(80, 200, 255, 255), 1.5f);
        }
        const float nx = L + (Rr - L) * (float)(d.bearingDeg / 360.0);
        dl->AddLine(ImVec2(nx, T), ImVec2(nx, B), IM_COL32(255, 80, 80, 255), 1.5f);
        ImGui::TextDisabled("Spectrum (dB, 0 at the peak) over bearing (deg)");
    }

    void drawHistory(const PipelineStatus& st) {
        if (st.history.empty()) { return; }
        ImGui::Text("Bearing history (%zu results)", st.history.size());
        if (ImGui::BeginTable(("kraken_hist_" + name).c_str(), 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders, ImVec2(0, 150))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Time");
            ImGui::TableSetupColumn("Bearing");
            ImGui::TableSetupColumn("Quality");
            ImGui::TableHeadersRow();
            ImGuiListClipper clip;
            clip.Begin((int)st.history.size());
            while (clip.Step()) {
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                    const auto& e = st.history[st.history.size() - 1 - i]; // newest first
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(clockString(e.wallTime).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.1f", e.bearingDeg);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.1f dB", e.qualityDb);
                }
            }
            ImGui::EndTable();
        }
    }

    // ------------------------------------------------------------ state
    std::string name;
    bool enabled = true;

    DoaPipeline pipe;
    DoaSettings settings;
    std::string host = "127.0.0.1";
    char hostBuf[128] = "127.0.0.1";
    int dataPort = 5000;
    int controlPort = 5001;
    bool controlWanted = false;
    double offsetKHz = 0.0;
    double bandwidthKHz = 12.5;
    double freqMHz = 0.0;
    bool freqTouched = false;
    int gainIdx = -1;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    std::string root = (std::string)core::args["root"];
    config.setPath(root + "/kraken_doa_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new KrakenDoaModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* inst) {
    delete (KrakenDoaModule*)inst;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
