#include "pipeline.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace kraken_doa {

    using Clock = std::chrono::steady_clock;

    const char* frameTypeName(uint32_t t) {
        switch (t) {
        case heimdall::FRAME_DATA: return "DATA";
        case heimdall::FRAME_DUMMY: return "DUMMY";
        case heimdall::FRAME_RAMP: return "RAMP";
        case heimdall::FRAME_CAL: return "CAL";
        case heimdall::FRAME_TRIGW: return "TRIGW";
        default: return "unknown";
        }
    }

    const char* syncStateName(uint32_t s) {
        switch (s) {
        case 1: return "INIT";
        case 2: return "sample cal";
        case 3: return "fractional cal";
        case 4: return "IQ cal";
        case 5: return "track lock";
        case 6: return "track";
        default: return "unknown";
        }
    }

    static std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
    static std::string fmt(const char* f, ...) {
        char buf[256];
        va_list ap;
        va_start(ap, f);
        vsnprintf(buf, sizeof(buf), f, ap);
        va_end(ap);
        return buf;
    }

    DoaPipeline::~DoaPipeline() {
        stop();
        {
            std::lock_guard<std::mutex> l(ctrlMtx);
            ctrlStop = true;
        }
        ctrlCv.notify_all();
        if (ctrlThread.joinable()) { ctrlThread.join(); }
    }

    void DoaPipeline::setSettings(const DoaSettings& s) {
        std::lock_guard<std::mutex> l(setMtx);
        settings = s;
    }

    void DoaPipeline::start(const std::string& host, int dataPort) {
        stop();
        {
            std::lock_guard<std::mutex> l(stMtx);
            ControlStatus c = pst.control;
            pst = PipelineStatus();
            pst.control = c;
            havePrevRx = false;
        }
        haveChanCfg = false;
        haveEstCfg = false;
        heimdall::IqClient::Config cfg;
        cfg.host = host;
        cfg.port = dataPort;
        iq.start(cfg);
        stopProc = false;
        active = true;
        procThread = std::thread(&DoaPipeline::processLoop, this);
    }

    void DoaPipeline::stop() {
        if (!active.exchange(false)) { return; }
        stopProc = true;
        if (procThread.joinable()) { procThread.join(); }
        iq.stop();
    }

    uint64_t DoaPipeline::processedCount() const {
        std::lock_guard<std::mutex> l(stMtx);
        return pst.framesProcessed;
    }

    PipelineStatus DoaPipeline::status() const {
        PipelineStatus s;
        {
            std::lock_guard<std::mutex> l(stMtx);
            s = pst;
        }
        s.iq = iq.stats();
        s.now = Clock::now();
        return s;
    }

    void DoaPipeline::setSkip(SkipKind k, const std::string& reason) {
        std::lock_guard<std::mutex> l(stMtx);
        pst.framesProcessed++; // counted when the outcome is visible
        pst.haveSkip = true;
        pst.skipKind = k;
        pst.skipReason = reason;
    }

    void DoaPipeline::processLoop() {
        heimdall::IqFrame frame;
        while (!stopProc) {
            if (!iq.takeLatest(frame)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            processFrame(frame, Clock::now());
        }
    }

    void DoaPipeline::processFrame(const heimdall::IqFrame& f, Clock::time_point rx) {
        const heimdall::IqHeader& h = f.header;
        DoaSettings s;
        {
            std::lock_guard<std::mutex> l(setMtx);
            s = settings;
        }
        {
            std::lock_guard<std::mutex> l(stMtx);
            if (havePrevRx) {
                double dt = std::chrono::duration<double>(rx - prevRx).count();
                pst.framePeriodSec = pst.framePeriodSec <= 0 ? dt : 0.8 * pst.framePeriodSec + 0.2 * dt;
            }
            havePrevRx = true;
            prevRx = rx;
            // Resync tracking after a frequency command
            if (pst.control.awaitingResync) {
                if (!heimdall::isUsableForDoa(h)) { awaitSawUnsync = true; }
                else if (awaitSawUnsync ||
                         std::fabs((double)h.rfCenterFreq - (double)pst.control.requestedFreqHz) < 1000.0) {
                    pst.control.awaitingResync = false;
                }
            }
        }

        if (!heimdall::isUsableForDoa(h)) {
            if (h.frameType != heimdall::FRAME_DATA) {
                setSkip(SkipKind::NotDataFrame, fmt("frame type %s, only DATA frames are used", frameTypeName(h.frameType)));
            }
            else {
                setSkip(SkipKind::NotSynced, fmt("DAQ not synchronized: state %s, delay_sync %u, iq_sync %u",
                                                  syncStateName(h.syncState), h.delaySyncFlag, h.iqSyncFlag));
            }
            return;
        }
        if (h.cpiLength == 0 || h.activeAntChs == 0 || f.samples.size() < (size_t)h.cpiLength * h.activeAntChs) {
            setSkip(SkipKind::EmptyFrame, "frame carries no samples");
            return;
        }
        if ((int)h.activeAntChs != s.elements) {
            setSkip(SkipKind::ChannelMismatch, fmt("array has %d elements, DAQ sends %u channels", s.elements, h.activeAntChs));
            return;
        }
        const double fs = (double)h.samplingFreq;
        if (fs <= 0 || std::fabs(s.offsetHz) >= fs / 2.0) {
            setSkip(SkipKind::BadSettings, fmt("signal offset %.1f kHz is outside the DAQ band (+-%.1f kHz)", s.offsetHz / 1e3, fs / 2e3));
            return;
        }
        if (s.bandwidthHz <= 0 || s.sizeMeters <= 0 || s.elements < 2) {
            setSkip(SkipKind::BadSettings, "bandwidth, array size or element count invalid");
            return;
        }
        const double sigFreq = (double)h.rfCenterFreq + s.offsetHz;
        if (sigFreq <= 0) {
            setSkip(SkipKind::BadSettings, "signal frequency is not positive");
            return;
        }

        auto t0 = Clock::now();

        dsp::ChannelizerConfig cc;
        cc.sampleRate = fs;
        cc.offsetHz = s.offsetHz;
        cc.bandwidthHz = s.bandwidthHz;
        if (!haveChanCfg || cc.sampleRate != chanCfg.sampleRate || cc.offsetHz != chanCfg.offsetHz ||
            cc.bandwidthHz != chanCfg.bandwidthHz) {
            chan.configure(cc);
            chanCfg = cc;
            haveChanCfg = true;
        }
        if (!chan.process(f.samples.data(), (int)h.activeAntChs, h.cpiLength) || chan.outputLength() < 16) {
            setSkip(SkipKind::BadSettings, "too few samples after channel selection (bandwidth too narrow for this frame)");
            return;
        }
        const float power = chan.powerDbfs();
        {
            std::lock_guard<std::mutex> l(stMtx);
            pst.havePower = true;
            pst.channelPowerDbfs = power;
        }
        if (power < s.squelchDbfs) {
            setSkip(SkipKind::BelowSquelch, fmt("below squelch (%.1f dBFS < %.1f dBFS)", power, s.squelchDbfs));
            return;
        }

        dsp::ArrayGeometry g;
        g.type = s.arrayType;
        g.elements = s.elements;
        g.sizeMeters = s.sizeMeters;
        dsp::DoaConfig dc;
        dc.algorithm = s.algorithm;
        dc.numSources = std::clamp(s.numSources, 1, std::max(1, s.elements - 1));
        dc.arrayOffsetDeg = s.arrayOffsetDeg;
        dc.compassOffsetDeg = s.compassOffsetDeg;
        dc.half = s.half;
        if (!haveEstCfg || g.type != estGeom.type || g.elements != estGeom.elements || g.sizeMeters != estGeom.sizeMeters ||
            sigFreq != estFreq || dc.algorithm != estCfg.algorithm || dc.numSources != estCfg.numSources ||
            dc.arrayOffsetDeg != estCfg.arrayOffsetDeg || dc.compassOffsetDeg != estCfg.compassOffsetDeg ||
            dc.half != estCfg.half) {
            std::vector<double> x, y;
            g.positions(sigFreq, x, y);
            haveEstCfg = estimator.configure(x, y, dc);
            estGeom = g;
            estCfg = dc;
            estFreq = sigFreq;
            if (!haveEstCfg) {
                setSkip(SkipKind::EstimatorFailed, "estimator rejected the array configuration");
                return;
            }
        }
        FrameResult r;
        if (!estimator.process(chan.output(), chan.outputLength(), r.doa)) {
            setSkip(SkipKind::EstimatorFailed, "estimator failed on this frame");
            return;
        }
        r.cpiIndex = h.cpiIndex;
        r.headerTimeStamp = h.timeStamp;
        r.receivedAt = rx;
        r.receivedWall = std::chrono::system_clock::now();
        r.powerDbfs = power;
        r.algorithm = s.algorithm;
        r.centerFreqHz = h.rfCenterFreq;
        r.sampleRateHz = h.samplingFreq;
        r.signalFreqHz = sigFreq;
        r.compassOffsetDeg = s.compassOffsetDeg;

        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        std::lock_guard<std::mutex> l(stMtx);
        pst.framesProcessed++;
        pst.processingMs = ms;
        pst.haveSkip = true;
        pst.skipKind = SkipKind::None;
        pst.skipReason.clear();
        HistoryEntry he;
        he.wallTime = r.receivedWall;
        he.bearingDeg = r.doa.bearingDeg;
        he.qualityDb = r.doa.peakQualityDb;
        he.cpiIndex = r.cpiIndex;
        pst.history.push_back(he);
        while (pst.history.size() > HISTORY_MAX) { pst.history.pop_front(); }
        pst.haveResult = true;
        pst.last = std::move(r);
    }

    // ---------------------------------------------------------------- control

    void DoaPipeline::enqueue(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> l(stMtx);
            pst.control.enabled = true;
            ctrlPending++;
            pst.control.busy = true;
        }
        {
            std::lock_guard<std::mutex> l(ctrlMtx);
            if (!ctrlThread.joinable()) { ctrlThread = std::thread(&DoaPipeline::controlLoop, this); }
            ctrlQueue.push_back(std::move(task));
        }
        ctrlCv.notify_one();
    }

    void DoaPipeline::controlLoop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> l(ctrlMtx);
                ctrlCv.wait(l, [this] { return ctrlStop || !ctrlQueue.empty(); });
                if (ctrlQueue.empty()) { return; } // stopping, nothing left
                task = std::move(ctrlQueue.front());
                ctrlQueue.pop_front();
            }
            task();
            std::lock_guard<std::mutex> l(stMtx);
            if (--ctrlPending <= 0) {
                ctrlPending = 0;
                pst.control.busy = false;
            }
        }
    }

    void DoaPipeline::connectControl(const std::string& host, int port) {
        enqueue([this, host, port] {
            bool ok = ctrl.connect(host, port);
            std::lock_guard<std::mutex> l(stMtx);
            pst.control.connected = ok;
            if (ok) {
                pst.control.lastError.clear();
                pst.control.lastInfo = "control connected";
            }
            else {
                pst.control.lastError = ctrl.lastError();
            }
        });
    }

    void DoaPipeline::disconnectControl() {
        {
            std::lock_guard<std::mutex> l(stMtx);
            pst.control.enabled = false;
        }
        enqueue([this] {
            ctrl.disconnect();
            std::lock_guard<std::mutex> l(stMtx);
            pst.control.connected = false;
            pst.control.enabled = false;
            pst.control.awaitingResync = false;
            pst.control.lastInfo = "control disconnected";
        });
    }

    void DoaPipeline::requestCenterFrequency(uint64_t hz) {
        enqueue([this, hz] {
            bool ok = ctrl.setCenterFrequency(hz);
            std::lock_guard<std::mutex> l(stMtx);
            pst.control.connected = ctrl.isConnected();
            if (ok) {
                pst.control.lastError.clear();
                pst.control.lastInfo = fmt("center frequency %.6f MHz acknowledged", hz / 1e6);
                pst.control.awaitingResync = true;
                pst.control.requestedFreqHz = hz;
                awaitSawUnsync = false;
            }
            else {
                pst.control.lastError = ctrl.lastError();
            }
        });
    }

    void DoaPipeline::requestGain(float gainDb, size_t channels) {
        enqueue([this, gainDb, channels] {
            bool ok = ctrl.setGains({ gainDb }, channels);
            std::lock_guard<std::mutex> l(stMtx);
            pst.control.connected = ctrl.isConnected();
            if (ok) {
                pst.control.lastError.clear();
                pst.control.lastInfo = fmt("gain %.1f dB set on %zu channels", gainDb, channels);
            }
            else {
                pst.control.lastError = ctrl.lastError();
            }
        });
    }

    void DoaPipeline::requestAgc() {
        enqueue([this] {
            bool ok = ctrl.setAgc();
            std::lock_guard<std::mutex> l(stMtx);
            pst.control.connected = ctrl.isConnected();
            if (ok) {
                pst.control.lastError.clear();
                pst.control.lastInfo = "AGC enabled";
            }
            else {
                pst.control.lastError = ctrl.lastError();
            }
        });
    }
}
