#include "coherent_bus.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <algorithm>

namespace rf_tools {
namespace doa {

#pragma pack(push, 1)
struct CRDFHeader {
    uint32_t magic;         // 'CRDF' (0x46445243)
    uint32_t channelCount;
    uint32_t sampleCount;
    uint64_t timestamp;
};
#pragma pack(pop)

static constexpr uint32_t CRDF_MAGIC = 0x46445243; // "CRDF" in little endian

CoherentBus::CoherentBus() {}

CoherentBus::~CoherentBus() {
    stopNetworkReceiver();
}

void CoherentBus::setSourceType(CoherentSourceType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    sourceType_ = type;
    snapshotQueue_.clear();
}

CoherentSourceType CoherentBus::getSourceType() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sourceType_;
}

void CoherentBus::pushFrame(const CoherentFrame& frame) {
    if (frame.channelCount == 0 || frame.sampleCount == 0) return;
    if (frame.channelData.size() < frame.channelCount) return;

    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t s = 0; s < frame.sampleCount; ++s) {
        std::vector<std::complex<float>> snapshot(frame.channelCount);
        for (size_t ch = 0; ch < frame.channelCount; ++ch) {
            if (s < frame.channelData[ch].size()) {
                snapshot[ch] = frame.channelData[ch][s];
            } else {
                snapshot[ch] = 0.0f;
            }
        }

        if (snapshotQueue_.size() >= MAX_SNAPSHOT_QUEUE) {
            snapshotQueue_.pop_front();
            framesDropped_++;
        }
        snapshotQueue_.push_back(std::move(snapshot));
    }
    framesReceived_++;
}

void CoherentBus::pushInterleavedSamples(const std::complex<float>* samples, size_t channelCount, size_t sampleCount) {
    if (!samples || channelCount == 0 || sampleCount == 0) return;

    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t s = 0; s < sampleCount; ++s) {
        std::vector<std::complex<float>> snapshot(channelCount);
        for (size_t ch = 0; ch < channelCount; ++ch) {
            snapshot[ch] = samples[s * channelCount + ch];
        }

        if (snapshotQueue_.size() >= MAX_SNAPSHOT_QUEUE) {
            snapshotQueue_.pop_front();
            framesDropped_++;
        }
        snapshotQueue_.push_back(std::move(snapshot));
    }
    framesReceived_++;
}

bool CoherentBus::getSnapshots(std::vector<std::complex<float>>& outInterleaved, size_t numChannels, size_t numSnapshots) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshotQueue_.size() < numSnapshots) {
        return false;
    }

    outInterleaved.resize(numSnapshots * numChannels);
    for (size_t k = 0; k < numSnapshots; ++k) {
        const auto& snap = snapshotQueue_.front();
        size_t availableCh = std::min(numChannels, snap.size());
        for (size_t ch = 0; ch < availableCh; ++ch) {
            outInterleaved[k * numChannels + ch] = snap[ch];
        }
        for (size_t ch = availableCh; ch < numChannels; ++ch) {
            outInterleaved[k * numChannels + ch] = 0.0f;
        }
        snapshotQueue_.pop_front();
    }

    return true;
}

size_t CoherentBus::getBufferedSnapshots() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshotQueue_.size();
}

void CoherentBus::resetStats() {
    framesReceived_ = 0;
    framesDropped_ = 0;
}

bool CoherentBus::startNetworkReceiver(uint16_t port) {
    stopNetworkReceiver();

    port_ = port;
    socketFd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd_ < 0) {
        return false;
    }

    // Set reuseaddr
    int opt = 1;
    setsockopt(socketFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Increase receive buffer to 2MB for high-rate I/Q streams
    int rcvbuf = 2 * 1024 * 1024;
    setsockopt(socketFd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(socketFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    networkRunning_ = true;
    networkThread_ = std::thread(&CoherentBus::networkWorkerLoop, this);
    return true;
}

void CoherentBus::stopNetworkReceiver() {
    if (networkRunning_.load()) {
        networkRunning_ = false;
        if (socketFd_ >= 0) {
            // Shutdown socket to break blocking recvfrom
            shutdown(socketFd_, SHUT_RDWR);
            close(socketFd_);
            socketFd_ = -1;
        }
        if (networkThread_.joinable()) {
            networkThread_.join();
        }
    }
}

bool CoherentBus::isNetworkReceiverRunning() const {
    return networkRunning_.load();
}

void CoherentBus::networkWorkerLoop() {
    // 64KB UDP packet buffer
    std::vector<uint8_t> buffer(65536);

    while (networkRunning_.load()) {
        struct sockaddr_in srcAddr;
        socklen_t addrLen = sizeof(srcAddr);

        ssize_t bytesRead = recvfrom(socketFd_, buffer.data(), buffer.size(), 0, (struct sockaddr*)&srcAddr, &addrLen);
        if (bytesRead < static_cast<ssize_t>(sizeof(CRDFHeader))) {
            if (!networkRunning_.load()) break;
            continue;
        }

        const CRDFHeader* hdr = reinterpret_cast<const CRDFHeader*>(buffer.data());
        if (hdr->magic == CRDF_MAGIC) {
            size_t expectedDataBytes = hdr->channelCount * hdr->sampleCount * sizeof(std::complex<float>);
            if (static_cast<size_t>(bytesRead) >= sizeof(CRDFHeader) + expectedDataBytes) {
                const std::complex<float>* payload = reinterpret_cast<const std::complex<float>*>(buffer.data() + sizeof(CRDFHeader));
                pushInterleavedSamples(payload, hdr->channelCount, hdr->sampleCount);
            }
        } else {
            // Raw interleaved cf32 without header (assume 4-channel fallback)
            size_t numSamples = bytesRead / (4 * sizeof(std::complex<float>));
            if (numSamples > 0) {
                const std::complex<float>* payload = reinterpret_cast<const std::complex<float>*>(buffer.data());
                pushInterleavedSamples(payload, 4, numSamples);
            }
        }
    }
}

} // namespace doa
} // namespace rf_tools
