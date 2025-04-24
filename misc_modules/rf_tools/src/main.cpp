#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <utils/flog.h>
#include <thread>
#include <algorithm>
#include <atomic>
#include "pdw/pulse_detector.h"
#include "pdw/pulse_extractor.h"
#include "pri/pri_analyzer.h"
#include "ui/pdw_panel.h"
#include "ui/histogram_panel.h"
#include "doa/types.h"
#include "doa/covariance_accumulator.h"
#include "doa/music_engine.h"
#include "doa/array_synthesizer.h"
#include "ui/doa_panel.h"

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

        createVFO();

        detector.init(sampleRate, -40.0f, 0.5e-6, 1000.0e-6);
        extractor.init(sampleRate, 0.0);
        priAnalyzer.init(256);

        // Initialize DOA components with 4-element ULA and 256 snapshots
        covAccumulator.init(4, 256, 0.0f, false);

        detector.setPulseCallback([this](const rf_tools::RawPulse& rawPulse) {
            double currentCf = gui::waterfall.getCenterFrequency() + (vfo ? vfo->getOffset() : 0.0);
            extractor.setCenterFrequency(currentCf);
            rf_tools::PDW pdw = extractor.extract(rawPulse);
            pdwPanel.addPDW(pdw);
            priAnalyzer.addPulse(pdw);
            histogramPanel.addPDW(pdw);
        });

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
                createVFO();
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
    // Up to 2 MHz, limited by the spectrum
    void createVFO() {
        double span = gui::waterfall.getBandwidth();
        double bw = (span > 0.0) ? std::min(2000000.0, span) : 2000000.0;
        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0.0, bw, bw, 50000.0, (span > 0.0) ? span : 10000000.0, false);
        if (!vfo) { return; }

        // Doesn't take clicks, select it from the menu
        vfo->wtfVFO->selectable = false;
        sampleRate = bw;
    }

    // Keep VFO, output rate and detector in sync
    void syncVFO() {
        double span = gui::waterfall.getBandwidth();
        double bw = vfo->getBandwidth();
        bool changed = vfo->getBandwidthChanged(true);

        if (span > 0.0) {
            vfo->setBandwidthLimits(50000.0, std::max(span, 50000.0), false);
            if (bw > span) {
                bw = span;
                changed = true;
            }
        }

        // Output rate follows the bandwidth
        if (changed) {
            vfo->setSampleRate(bw, bw);
        }

        if (bw != sampleRate) {
            sampleRate = bw;
            detector.setSampleRate(bw);
            extractor.setSampleRate(bw);
        }
    }

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

            syncVFO();

            int count = vfo->output->read();
            if (count < 0) {
                break;
            }

            detector.process(vfo->output->readBuf, count);

            // Direction Finding (DOA) Processing
            if (doaEnabled.load()) {
                if (doaSimulationMode.load()) {
                    auto synth_snapshots = arraySynthesizer.generateSnapshots(256, static_cast<float>(sampleRate));
                    covAccumulator.feedSnapshotsInterleaved(synth_snapshots.data(), musicEngine.getConfig().num_elements, 256);
                } else {
                    // Feed single-channel stream to array channel 0
                    std::vector<std::complex<float>> ch_snap(musicEngine.getConfig().num_elements, 0.0f);
                    for (int i = 0; i < count; ++i) {
                        ch_snap[0] = std::complex<float>(vfo->output->readBuf[i].re, vfo->output->readBuf[i].im);
                        covAccumulator.feedSnapshot(ch_snap.data(), musicEngine.getConfig().num_elements);
                    }
                }

                Eigen::MatrixXcf Rxx;
                if (covAccumulator.getNormalizedCovariance(Rxx)) {
                    rf_tools::doa::DOAResult res = musicEngine.computeDOA(Rxx, musicEngine.getConfig().num_snapshots);
                    doaPanel.updateResult(res);
                }
            }

            vfo->output->flush();
        }
    }

    static void menuHandler(void* ctx) {
        RFAnalysisModule* mod = static_cast<RFAnalysisModule*>(ctx);
        if (!mod) return;

        double centerFreq = gui::waterfall.getCenterFrequency() + (mod->vfo ? mod->vfo->getOffset() : 0.0);
        double vfoBw = mod->vfo ? mod->vfo->getBandwidth() : 0.0;

        if (mod->vfo) {
            if (gui::waterfall.selectedVFO == mod->name) {
                ImGui::TextDisabled("VFO selected: click the spectrum to move it");
            }
            else {
                if (ImGui::Button("Select VFO##rf_tools_select_vfo")) {
                    gui::waterfall.selectedVFO = mod->name;
                    gui::waterfall.selectedVFOChanged = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("This VFO ignores clicks on the spectrum so it doesn't take over tuning.\nSelect it to move it or to see its frequency.");
                }
            }
            ImGui::Spacing();
        }

        if (ImGui::BeginTabBar("##rf_tools_tabs")) {
            if (ImGui::BeginTabItem("Pulse / PDW")) {
                mod->pdwPanel.draw(mod->detector, centerFreq, vfoBw);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Histograms")) {
                mod->histogramPanel.draw(mod->priAnalyzer);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Direction Finding (DOA)")) {
                bool doa_on = mod->doaEnabled.load();
                if (ImGui::Checkbox("Enable DOA Processing##rf_doa_on", &doa_on)) {
                    mod->doaEnabled.store(doa_on);
                }
                ImGui::SameLine();
                bool sim_on = mod->doaSimulationMode.load();
                if (ImGui::Checkbox("Virtual Array Simulator##rf_doa_sim", &sim_on)) {
                    mod->doaSimulationMode.store(sim_on);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Simulates multi-channel spatial wavefronts for real-time validation of MUSIC and Root-MUSIC.");
                }
                ImGui::Spacing();
                mod->doaPanel.draw(mod->musicEngine, mod->covAccumulator);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    std::string name;
    bool enabled = true;
    VFOManager::VFO* vfo = nullptr;
    double sampleRate = 2000000.0;

    rf_tools::PulseDetector detector;
    rf_tools::PulseExtractor extractor;
    rf_tools::PRIAnalyzer priAnalyzer;
    rf_tools::PDWPanel pdwPanel;
    rf_tools::HistogramPanel histogramPanel;

    // Direction Finding components
    rf_tools::doa::CovarianceAccumulator covAccumulator;
    rf_tools::doa::MUSICEngine musicEngine;
    rf_tools::doa::ArraySynthesizer arraySynthesizer;
    rf_tools::DOAPanel doaPanel;
    std::atomic<bool> doaEnabled{ true };
    std::atomic<bool> doaSimulationMode{ true };

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
