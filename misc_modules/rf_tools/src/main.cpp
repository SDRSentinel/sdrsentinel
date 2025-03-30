#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <utils/flog.h>
#include <thread>
#include <atomic>
#include "pdw/pulse_detector.h"

SDRPP_MOD_INFO{
    /* Name:            */ "rf_tools",
    /* Description:     */ "Pulse & RF Analysis Suite for SDR++",
    /* Author:          */ "Suleyman Poyraz",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

class RFAnalysisModule : public ModuleManager::Instance {
public:
    RFAnalysisModule(std::string name) {
        this->name = name;

        // Default VFO parameters: 2 MHz bandwidth, centered
        double defaultBw = 2000000.0;
        double defaultSr = 2000000.0;
        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0.0, defaultBw, defaultSr, 50000.0, 10000000.0, false);

        detector.init(defaultSr, -40.0f, 0.5e-6, 1000.0e-6);

        startWorker();
        gui::menu.registerEntry(name, menuHandler, this, this);
        flog::info("RF Tools module instance '{0}' initialized", name);
    }

    ~RFAnalysisModule() {
        gui::menu.removeEntry(name);
        stopWorker();
        if (vfo) {
            sigpath::vfoManager.deleteVFO(vfo);
            vfo = nullptr;
        }
        flog::info("RF Tools module instance '{0}' destroyed", name);
    }

    void postInit() override {}

    void enable() override {
        if (!enabled) {
            if (!vfo) {
                vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0.0, 2000000.0, 2000000.0, 50000.0, 10000000.0, false);
            }
            startWorker();
            enabled = true;
        }
    }

    void disable() override {
        if (enabled) {
            stopWorker();
            if (vfo) {
                sigpath::vfoManager.deleteVFO(vfo);
                vfo = nullptr;
            }
            enabled = false;
        }
    }

    bool isEnabled() override {
        return enabled;
    }

private:
    void startWorker() {
        if (running.load() || !vfo || !vfo->output) return;
        running.store(true);
        workerThread = std::thread(&RFAnalysisModule::workerLoop, this);
    }

    void stopWorker() {
        if (!running.load()) return;
        running.store(false);
        if (vfo && vfo->output) {
            vfo->output->stopReader();
        }
        if (workerThread.joinable()) {
            workerThread.join();
        }
        if (vfo && vfo->output) {
            vfo->output->clearReadStop();
        }
    }

    void workerLoop() {
        while (running.load()) {
            if (!vfo || !vfo->output) break;
            int count = vfo->output->read();
            if (count < 0) {
                break;
            }

            // Check if VFO sample rate or bandwidth changed
            double currentBw = vfo->getBandwidth();
            detector.setSampleRate(currentBw);

            detector.process(vfo->output->readBuf, count);
            vfo->output->flush();
        }
    }

    static void menuHandler(void* ctx) {
        RFAnalysisModule* mod = static_cast<RFAnalysisModule*>(ctx);
        if (!mod) return;

        double centerFreq = gui::waterfall.getCenterFrequency() + (mod->vfo ? mod->vfo->getOffset() : 0.0);
        double vfoBw = mod->vfo ? mod->vfo->getBandwidth() : 0.0;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        if (ImGui::BeginTabBar("##rf_tools_tabs")) {
            if (ImGui::BeginTabItem("Pulse / PDW")) {
                ImGui::Spacing();
                ImGui::Text("VFO: CF = %.3f MHz | BW = %.3f MHz", centerFreq / 1.0e6, vfoBw / 1.0e6);
                ImGui::Separator();

                float thresh = mod->detector.getThreshold();
                ImGui::LeftLabel("Threshold (dBFS)");
                ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
                if (ImGui::SliderFloat("##rf_pdw_thresh", &thresh, -100.0f, -5.0f, "%.1f dBFS")) {
                    mod->detector.setThreshold(thresh);
                }

                float minPwUs = static_cast<float>(mod->detector.getMinPulseWidth() * 1.0e6);
                ImGui::LeftLabel("Min PW (us)");
                ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
                if (ImGui::SliderFloat("##rf_pdw_min_pw", &minPwUs, 0.1f, 50.0f, "%.2f us")) {
                    mod->detector.setPulseWidthLimits(minPwUs * 1.0e-6, mod->detector.getMaxPulseWidth());
                }

                float maxPwUs = static_cast<float>(mod->detector.getMaxPulseWidth() * 1.0e6);
                ImGui::LeftLabel("Max PW (us)");
                ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
                if (ImGui::SliderFloat("##rf_pdw_max_pw", &maxPwUs, 10.0f, 2000.0f, "%.1f us")) {
                    mod->detector.setPulseWidthLimits(mod->detector.getMinPulseWidth(), maxPwUs * 1.0e-6);
                }

                ImGui::Spacing();
                ImGui::Text("Detected Pulses: %llu", (unsigned long long)mod->detector.getTotalPulses());

                if (ImGui::Button("Reset Counter##rf_pdw_reset")) {
                    mod->detector.reset();
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Milestone 1 Active: Real-time Pulse Detection");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Histograms")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "PRI, PW & RF Distribution Histograms");
                ImGui::Separator();
                ImGui::TextUnformatted("(Upcoming Milestone 3: PRI Stagger & Jitter Analysis)");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Direction Finding")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Direction of Arrival (DOA) / MUSIC Engine");
                ImGui::Separator();
                ImGui::TextUnformatted("(Upcoming Milestone 4 & 5: Multi-channel MUSIC & Root-MUSIC)");
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    std::string name;
    bool enabled = true;
    VFOManager::VFO* vfo = nullptr;

    rf_tools::PulseDetector detector;

    std::thread workerThread;
    std::atomic<bool> running{ false };
};

MOD_EXPORT void _INIT_() {
    flog::info("RF Tools plugin loaded");
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new RFAnalysisModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* inst) {
    delete static_cast<RFAnalysisModule*>(inst);
}

MOD_EXPORT void _END_() {
    flog::info("RF Tools plugin unloaded");
}
