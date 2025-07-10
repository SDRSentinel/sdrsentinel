#include "control_client.h"
#include <cmath>
#include <cstring>

namespace kraken_doa::heimdall {

    ControlClient::~ControlClient() { disconnect(); }

    const std::vector<uint32_t>& ControlClient::acceptedGainsTenthDb() {
        static const std::vector<uint32_t> g = { 0, 9, 14, 27, 37, 77, 87, 125, 144, 157, 166, 197, 207, 229, 254,
                                                  280, 297, 328, 338, 364, 372, 386, 402, 421, 434, 439, 445, 480, 496 };
        return g;
    }

    std::vector<float> ControlClient::acceptedGainsDb() {
        std::vector<float> out;
        for (uint32_t g : acceptedGainsTenthDb()) { out.push_back((float)g / 10.0f); }
        return out;
    }

    uint32_t ControlClient::snapGainTenthDb(float gainDb) {
        const auto& list = acceptedGainsTenthDb();
        double t = std::isfinite(gainDb) ? (double)gainDb * 10.0 : 0.0;
        uint32_t best = list.front();
        double bestD = std::fabs(t - (double)best);
        for (uint32_t g : list) {
            double d = std::fabs(t - (double)g);
            if (d < bestD) { best = g; bestD = d; }
        }
        return best;
    }

    bool ControlClient::connect(const std::string& host, int port, int connectTimeoutMs) {
        std::lock_guard<std::mutex> l(mtx);
        closeLocked();
        std::string err;
        sock = tcp::connectTo(host, port, connectTimeoutMs, 0, err);
        if (sock == tcp::INVALID) {
            error = err;
            return false;
        }
        error.clear();
        return true;
    }

    void ControlClient::disconnect() {
        std::lock_guard<std::mutex> l(mtx);
        if (sock != tcp::INVALID) {
            uint8_t msg[MSG_SIZE] = { 0 };
            std::memcpy(msg, "EXIT", 4);
            tcp::sendAll(sock, msg, MSG_SIZE);
        }
        closeLocked();
    }

    void ControlClient::closeLocked() {
        if (sock != tcp::INVALID) {
            tcp::shutdownBoth(sock);
            tcp::closeSocket(sock);
            sock = tcp::INVALID;
        }
    }

    bool ControlClient::isConnected() const {
        std::lock_guard<std::mutex> l(mtx);
        return sock != tcp::INVALID;
    }

    std::string ControlClient::lastError() const {
        std::lock_guard<std::mutex> l(mtx);
        return error;
    }

    bool ControlClient::transact(const char cmd[4], const uint8_t* payload, size_t payloadLen) {
        std::lock_guard<std::mutex> l(mtx);
        if (sock == tcp::INVALID) {
            error = "not connected";
            return false;
        }
        uint8_t msg[MSG_SIZE] = { 0 };
        std::memcpy(msg, cmd, 4);
        if (payloadLen) { std::memcpy(msg + 4, payload, payloadLen); }
        if (!tcp::sendAll(sock, msg, MSG_SIZE)) {
            error = "send failed";
            closeLocked();
            return false;
        }
        tcp::setRecvTimeout(sock, replyTimeoutMs);
        uint8_t reply[MSG_SIZE];
        bool timedOut = false;
        if (!tcp::recvExact(sock, reply, MSG_SIZE, &timedOut)) {
            error = timedOut ? "timed out waiting for reply" : "connection lost waiting for reply";
            closeLocked();
            return false;
        }
        if (std::memcmp(reply, "FNSD", 4) != 0) {
            error = "unexpected reply (no FNSD)";
            return false;
        }
        error.clear();
        return true;
    }

    bool ControlClient::init() { return transact("INIT", nullptr, 0); }

    bool ControlClient::setAgc() { return transact("AGC ", nullptr, 0); }

    bool ControlClient::setCenterFrequency(uint64_t hz) {
        uint8_t p[8];
        for (int i = 0; i < 8; i++) { p[i] = (uint8_t)(hz >> (8 * i)); }
        return transact("FREQ", p, 8);
    }

    bool ControlClient::setGains(const std::vector<float>& gainsDb, size_t channels) {
        if (channels == 0 || channels > (MSG_SIZE - 4) / 4) {
            std::lock_guard<std::mutex> l(mtx);
            error = "invalid channel count for GAIN";
            return false;
        }
        if (gainsDb.size() != channels && gainsDb.size() != 1) {
            std::lock_guard<std::mutex> l(mtx);
            error = "gain list size does not match channel count";
            return false;
        }
        uint8_t p[MSG_SIZE - 4] = { 0 };
        for (size_t c = 0; c < channels; c++) {
            uint32_t v = snapGainTenthDb(gainsDb.size() == 1 ? gainsDb[0] : gainsDb[c]);
            for (int i = 0; i < 4; i++) { p[4 * c + i] = (uint8_t)(v >> (8 * i)); }
        }
        return transact("GAIN", p, channels * 4);
    }
}
