#include "sdrpp_probe.h"
#include <gui/gui.h>
#include <gui/tuner.h>
#include <signal_path/signal_path.h>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace rf_tools {
namespace vna {

struct SdrppScalarProbe::Impl {
    VFOManager::VFO* vfo = nullptr;
    std::string vfoName;
    double offsetHz = 0.0;
    double sampleRate = 0.0;
    double originalCenter = 0.0;
    std::string originalSelectedVfo;
    bool retuned = false;
    std::chrono::steady_clock::time_point tuneTime;
};

std::unique_ptr<SdrppScalarProbe> SdrppScalarProbe::create(const std::string& moduleName, const ScalarSweepSettings& s, std::string& err) {
    if (!gui::mainWindow.isPlaying()) {
        err = "Start the SDRSentinel source first (play button): the sweep measures through the running receiver";
        return nullptr;
    }
    double span = gui::waterfall.getBandwidth();
    if (!(span > 0.0) || std::fabs(s.offsetHz) + s.rbwHz / 2.0 > span / 2.0) {
        err = "Offset plus RBW does not fit in the source bandwidth: reduce the offset or increase the source sample rate";
        return nullptr;
    }

    std::string vfoName = moduleName + " sweep";
    if (sigpath::vfoManager.vfoExists(vfoName)) {
        err = "A sweep VFO already exists";
        return nullptr;
    }

    std::unique_ptr<SdrppScalarProbe> p(new SdrppScalarProbe());
    p->impl_.reset(new Impl());
    // Creating and deleting a VFO changes the waterfall selection; restore it afterwards
    p->impl_->originalSelectedVfo = gui::waterfall.selectedVFO;
    p->impl_->vfo = sigpath::vfoManager.createVFO(vfoName, ImGui::WaterfallVFO::REF_CENTER, s.offsetHz, s.rbwHz, s.rbwHz, s.rbwHz, s.rbwHz, true);
    if (!p->impl_->vfo) {
        err = "Could not create the sweep VFO";
        return nullptr;
    }
    p->impl_->vfoName = vfoName;
    p->impl_->offsetHz = s.offsetHz;
    p->impl_->sampleRate = s.rbwHz;
    p->impl_->originalCenter = gui::waterfall.getCenterFrequency();
    // Doesn't take clicks, so it never becomes the tuned VFO
    p->impl_->vfo->wtfVFO->selectable = false;
    return p;
}

SdrppScalarProbe::~SdrppScalarProbe() {
    if (impl_ && impl_->vfo) {
        sigpath::vfoManager.deleteVFO(impl_->vfo);
        impl_->vfo = nullptr;
        const std::string& sel = impl_->originalSelectedVfo;
        if (!sel.empty() && gui::waterfall.vfos.find(sel) != gui::waterfall.vfos.end()) {
            gui::waterfall.selectedVFO = sel;
            gui::waterfall.selectedVFOChanged = true;
        }
    }
}

bool SdrppScalarProbe::tune(double hz) {
    // The VFO sits offsetHz above the center, so the center goes offsetHz below the target
    double center = hz - impl_->offsetHz;
    if (center <= 0.0) { error_ = "Target below the tunable range"; return false; }
    tuner::centerTuning("", center);
    impl_->retuned = true;
    impl_->tuneTime = std::chrono::steady_clock::now();
    return true;
}

bool SdrppScalarProbe::measurePowerDb(double settleSec, double dwellSec, float& outDb) {
    dsp::stream<dsp::complex_t>* out = impl_->vfo->output;
    const size_t settleSamples = (size_t)(settleSec * impl_->sampleRate);
    const size_t dwellSamples = std::max<size_t>(1, (size_t)(dwellSec * impl_->sampleRate));
    const auto settleEnd = impl_->tuneTime + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(settleSec));

    // Anything read before the settle time (and a block more) may predate the retune: throw it away
    size_t discarded = 0;
    bool settled = false;
    while (!settled) {
        int count = out->read();
        if (count < 0) { error_ = "Receiver stream stopped"; return false; }
        discarded += count;
        out->flush();
        settled = discarded >= settleSamples && std::chrono::steady_clock::now() >= settleEnd;
    }
    {
        int count = out->read();
        if (count < 0) { error_ = "Receiver stream stopped"; return false; }
        out->flush();
    }

    double sum = 0.0;
    size_t n = 0;
    while (n < dwellSamples) {
        int count = out->read();
        if (count < 0) { error_ = "Receiver stream stopped"; return false; }
        size_t take = std::min<size_t>((size_t)count, dwellSamples - n);
        for (size_t i = 0; i < take; ++i) {
            const dsp::complex_t& x = out->readBuf[i];
            sum += (double)x.re * x.re + (double)x.im * x.im;
        }
        n += take;
        out->flush();
    }

    double mean = sum / (double)n;
    outDb = (mean > 1e-20) ? (float)(10.0 * std::log10(mean)) : -200.0f;
    return true;
}

void SdrppScalarProbe::requestCancel() {
    if (impl_ && impl_->vfo && impl_->vfo->output) { impl_->vfo->output->stopReader(); }
}

void SdrppScalarProbe::finish() {
    if (impl_->retuned) {
        tuner::centerTuning("", impl_->originalCenter);
        impl_->retuned = false;
    }
}

} // namespace vna
} // namespace rf_tools
