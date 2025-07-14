#pragma once
#include "iq_header.h"
#include "tcp_util.h"
#include <atomic>
#include <chrono>
#include <complex>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kraken_doa::heimdall {

    // One decoded frame. samples is channel-major: samples[ch * header.cpiLength + n].
    struct IqFrame {
        IqHeader header;
        std::vector<std::complex<float>> samples;
    };

    // AwaitingFrame: TCP connected and request sent, but no valid Heimdall header received yet.
    // Only a parsed header proves the peer is a Heimdall IQ server.
    enum class ConnState { Disconnected, Connecting, AwaitingFrame, Streaming, Error };

    struct IqStats {
        ConnState state = ConnState::Disconnected;
        std::string errorMessage;
        uint64_t framesReceived = 0;
        uint64_t framesUsable = 0;
        uint64_t framesByType[5] = { 0 }; // indexed by FrameType, other values are not counted here
        uint64_t cpiGaps = 0;             // frames missing according to cpi_index jumps (DUMMY frames ignored)
        uint64_t framesOverwritten = 0;   // mailbox frames replaced before the consumer took them
        uint64_t bytesReceived = 0;
        double bytesPerSecond = 0;        // over the last ~1 s window
        IqHeader lastHeader;
        bool haveLastHeader = false;
    };

    class IqClient {
    public:
        struct Config {
            std::string host = "127.0.0.1";
            int port = 5000;
            int connectTimeoutMs = 3000;
            int firstFrameTimeoutMs = 10000; // no valid header within this time after connecting is an error
            int rcvBufBytes = 32 * 1024 * 1024;
            uint64_t maxPayloadBytes = DEFAULT_MAX_PAYLOAD_BYTES;
            bool autoReconnect = false;
            int reconnectDelayMs = 1000;
        };

        IqClient() = default;
        ~IqClient();
        IqClient(const IqClient&) = delete;
        IqClient& operator=(const IqClient&) = delete;

        // Consumer callback, runs on the receive thread. If set, frames are not placed in the mailbox.
        // The frame reference is only valid during the call. Set before start().
        void setFrameCallback(std::function<void(const IqFrame&)> cb) { callback = std::move(cb); }

        void start(const Config& cfg);
        void stop(); // thread safe, blocks until the receive thread has ended

        // Mailbox: moves the newest unread frame into out (swapping buffers). False if none.
        bool takeLatest(IqFrame& out);

        ConnState state() const { return connState.load(); }
        IqStats stats() const;

    private:
        void run();
        bool session();
        void setState(ConnState s, const std::string& msg = "");
        void deliver(IqFrame& f);
        void account(const IqHeader& h, uint64_t bytes);

        Config cfg;
        std::function<void(const IqFrame&)> callback;
        std::thread worker;
        std::atomic<bool> stopFlag{ false };
        std::atomic<ConnState> connState{ ConnState::Disconnected };

        std::mutex sockMtx; // guards sock and all sends
        tcp::Socket sock = tcp::INVALID;

        mutable std::mutex statsMtx;
        IqStats st;
        bool haveCpi = false;
        uint32_t lastCpi = 0;
        std::chrono::steady_clock::time_point rateT0;
        uint64_t rateBytes0 = 0;

        std::mutex boxMtx;
        IqFrame box;
        bool boxFull = false;

        IqFrame work;
        std::vector<uint8_t> raw;
    };
}
