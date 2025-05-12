#pragma once
#include <vector>
#include <complex>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>
#include <cstdint>
#include <string>

namespace rf_tools {
namespace doa {

enum class CoherentSourceType {
    VIRTUAL_SIMULATOR = 0,
    NETWORK_STREAM = 1,     // UDP / KrakenSDR Heimdall DAQ format
    SOFTWARE_MULTI_VFO = 2  // Multi-VFO phase/frequency channelizer
};

struct CoherentFrame {
    uint64_t timestamp = 0;
    uint32_t channelCount = 0;
    uint32_t sampleCount = 0;
    // Samples stored as channelData[channel][sample]
    std::vector<std::vector<std::complex<float>>> channelData;
};

class CoherentBus {
public:
    CoherentBus();
    ~CoherentBus();

    void setSourceType(CoherentSourceType type);
    CoherentSourceType getSourceType() const;

    // Direct frame injection
    void pushFrame(const CoherentFrame& frame);
    void pushInterleavedSamples(const std::complex<float>* samples, size_t channelCount, size_t sampleCount);

    // Retrieve interleaved snapshots [K snapshots x M channels] for CovarianceAccumulator
    bool getSnapshots(std::vector<std::complex<float>>& outInterleaved, size_t numChannels, size_t numSnapshots);

    // Network UDP receiver (for KrakenSDR / SDR++ Coherent stream)
    bool startNetworkReceiver(uint16_t port = 5000);
    void stopNetworkReceiver();
    bool isNetworkReceiverRunning() const;
    uint16_t getNetworkPort() const { return port_; }

    // Statistics
    uint64_t getFramesReceived() const { return framesReceived_.load(); }
    uint64_t getFramesDropped() const { return framesDropped_.load(); }
    size_t getBufferedSnapshots() const;
    void resetStats();

private:
    void networkWorkerLoop();

    CoherentSourceType sourceType_ = CoherentSourceType::VIRTUAL_SIMULATOR;
    mutable std::mutex mutex_;

    // Ring buffer of interleaved snapshots: vector of size M for each snapshot
    std::deque<std::vector<std::complex<float>>> snapshotQueue_;
    static constexpr size_t MAX_SNAPSHOT_QUEUE = 16384;

    std::atomic<uint64_t> framesReceived_{ 0 };
    std::atomic<uint64_t> framesDropped_{ 0 };

    // UDP receiver thread state
    std::atomic<bool> networkRunning_{ false };
    std::thread networkThread_;
    int socketFd_ = -1;
    uint16_t port_ = 5000;
};

} // namespace doa
} // namespace rf_tools
