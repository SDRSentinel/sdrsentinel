#pragma once
// Frame to bearing pipeline for the KrakenSDR module. No UI code. A bearing is only ever produced from a
// frame that passed the gate (sync word, DATA, delay and IQ sync flags), has the configured element count,
// and is above the squelch level. Everything else is recorded as a skip reason.
#include "dsp/array_geometry.h"
#include "dsp/channelizer.h"
#include "dsp/doa_estimator.h"
#include "heimdall/control_client.h"
#include "heimdall/iq_client.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kraken_doa {

    struct DoaSettings {
        // Signal
        double offsetHz = 0.0;
        double bandwidthHz = 12500.0;
        double squelchDbfs = -60.0;
        // Array
        dsp::ArrayType arrayType = dsp::ArrayType::UCA;
        int elements = 5;
        double sizeMeters = 0.17; // UCA radius or ULA spacing
        double arrayOffsetDeg = 0.0;
        double compassOffsetDeg = 0.0;
        dsp::ScanHalf half = dsp::ScanHalf::Both;
        dsp::DoaAlgorithm algorithm = dsp::DoaAlgorithm::Music;
        int numSources = 1;
    };

    enum class SkipKind {
        None,          // last frame produced a result
        NotDataFrame,  // CAL, DUMMY, RAMP, TRIGW
        NotSynced,     // DATA frame, sync flags not both set
        EmptyFrame,
        ChannelMismatch,
        BadSettings,
        BelowSquelch,
        EstimatorFailed
    };

    const char* frameTypeName(uint32_t t);
    const char* syncStateName(uint32_t s);

    struct HistoryEntry {
        std::chrono::system_clock::time_point wallTime;
        double bearingDeg = 0.0;
        double qualityDb = 0.0;
        uint32_t cpiIndex = 0;
    };

    struct FrameResult {
        dsp::DoaResult doa;
        uint32_t cpiIndex = 0;
        uint64_t headerTimeStamp = 0;
        std::chrono::steady_clock::time_point receivedAt;
        std::chrono::system_clock::time_point receivedWall;
        float powerDbfs = 0;
        dsp::DoaAlgorithm algorithm = dsp::DoaAlgorithm::Music;
        uint64_t centerFreqHz = 0;  // fc of the processed frame
        uint64_t sampleRateHz = 0;  // fs of the processed frame
        double signalFreqHz = 0;    // fc + offset used for the geometry
        double compassOffsetDeg = 0; // setting in effect when this result was computed
    };

    struct ControlStatus {
        bool enabled = false;      // the user asked for control
        bool connected = false;
        bool busy = false;         // a command is in flight
        std::string lastError;
        std::string lastInfo;      // e.g. "frequency command acknowledged"
        bool awaitingResync = false; // FREQ acknowledged, sync flags not back yet
        uint64_t requestedFreqHz = 0;
    };

    struct PipelineStatus {
        heimdall::IqStats iq;
        ControlStatus control;
        uint64_t framesProcessed = 0; // frames taken by the processing thread
        bool haveSkip = false;        // a processed frame exists
        SkipKind skipKind = SkipKind::None;
        std::string skipReason;       // for the most recent processed frame, empty when it produced a result
        bool havePower = false;
        float channelPowerDbfs = 0;
        double processingMs = 0;      // for the last frame that reached the DSP
        double framePeriodSec = 0;    // smoothed interval between processed frames
        bool haveResult = false;
        FrameResult last;             // most recent real result
        std::deque<HistoryEntry> history;
        std::chrono::steady_clock::time_point now;
    };

    class DoaPipeline {
    public:
        static constexpr size_t HISTORY_MAX = 300;

        DoaPipeline() = default;
        ~DoaPipeline();
        DoaPipeline(const DoaPipeline&) = delete;
        DoaPipeline& operator=(const DoaPipeline&) = delete;

        void setSettings(const DoaSettings& s);

        void start(const std::string& host, int dataPort);
        void stop();
        bool running() const { return active.load(); }

        PipelineStatus status() const;

        // Control (optional, off the calling thread, never blocks)
        void connectControl(const std::string& host, int port);
        void disconnectControl();
        void requestCenterFrequency(uint64_t hz);
        void requestGain(float gainDb, size_t channels);
        void requestAgc();

        // Frames whose outcome (result or skip reason) is already visible in status()
        uint64_t processedCount() const;

    private:
        void processLoop();
        void processFrame(const heimdall::IqFrame& f, std::chrono::steady_clock::time_point rx);
        void setSkip(SkipKind k, const std::string& reason);
        void controlLoop();
        void enqueue(std::function<void()> task);

        heimdall::IqClient iq;
        std::atomic<bool> active{ false };
        std::atomic<bool> stopProc{ false };
        std::thread procThread;

        mutable std::mutex setMtx;
        DoaSettings settings;

        // DSP state, only touched by the processing thread
        dsp::Channelizer chan;
        dsp::DoaEstimator estimator;
        bool haveChanCfg = false, haveEstCfg = false;
        dsp::ChannelizerConfig chanCfg;
        dsp::ArrayGeometry estGeom;
        dsp::DoaConfig estCfg;
        double estFreq = 0;
        bool havePrevRx = false;
        std::chrono::steady_clock::time_point prevRx;

        mutable std::mutex stMtx;
        PipelineStatus pst;

        // Control worker
        heimdall::ControlClient ctrl;
        std::thread ctrlThread;
        std::mutex ctrlMtx;
        std::condition_variable ctrlCv;
        std::deque<std::function<void()>> ctrlQueue;
        bool ctrlStop = false;
        bool awaitSawUnsync = false; // guarded by stMtx
        int ctrlPending = 0;         // guarded by stMtx
    };
}
