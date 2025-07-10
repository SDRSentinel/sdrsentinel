#pragma once
#include "tcp_util.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace kraken_doa::heimdall {

    // Client for the Heimdall hw_controller (port 5001). Every message is exactly 128 bytes,
    // every command except EXIT is answered with 128 bytes starting with "FNSD".
    class ControlClient {
    public:
        static constexpr size_t MSG_SIZE = 128;

        ControlClient() = default;
        ~ControlClient();
        ControlClient(const ControlClient&) = delete;
        ControlClient& operator=(const ControlClient&) = delete;

        bool connect(const std::string& host, int port = 5001, int connectTimeoutMs = 3000);
        void disconnect(); // sends EXIT, no reply expected
        bool isConnected() const;

        // Reply timeout for every command (default 5000 ms). A timeout closes the connection
        // because the reply stream would be out of step afterwards.
        void setReplyTimeoutMs(int ms) { replyTimeoutMs = ms; }

        bool init();
        bool setCenterFrequency(uint64_t hz);
        // One value per channel (dB) or a single value applied to all; snapped to the accepted list.
        // channels is M from the IQ header's active_ant_chs (at most 31 fit in one message).
        bool setGains(const std::vector<float>& gainsDb, size_t channels);
        bool setAgc();

        std::string lastError() const;

        // Accepted gains in dB x 10, and in dB for the UI.
        static const std::vector<uint32_t>& acceptedGainsTenthDb();
        static std::vector<float> acceptedGainsDb();
        static uint32_t snapGainTenthDb(float gainDb);

    private:
        bool transact(const char cmd[4], const uint8_t* payload, size_t payloadLen);
        void closeLocked();

        mutable std::mutex mtx;
        tcp::Socket sock = tcp::INVALID;
        int replyTimeoutMs = 5000;
        std::string error;
    };
}
