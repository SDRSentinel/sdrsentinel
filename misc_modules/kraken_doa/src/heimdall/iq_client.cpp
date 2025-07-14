#include "iq_client.h"
#include <cstring>

namespace kraken_doa::heimdall {

    IqClient::~IqClient() { stop(); }

    void IqClient::start(const Config& c) {
        stop();
        cfg = c;
        stopFlag = false;
        {
            std::lock_guard<std::mutex> l(statsMtx);
            st = IqStats();
            haveCpi = false;
        }
        {
            std::lock_guard<std::mutex> l(boxMtx);
            boxFull = false;
        }
        setState(ConnState::Connecting);
        worker = std::thread(&IqClient::run, this);
    }

    void IqClient::stop() {
        stopFlag = true;
        {
            std::lock_guard<std::mutex> l(sockMtx);
            if (sock != tcp::INVALID) {
                tcp::sendAll(sock, "q", 1); // polite end of session, best effort
                tcp::shutdownBoth(sock);    // unblocks a pending recv
            }
        }
        if (worker.joinable()) { worker.join(); }
        if (connState.load() != ConnState::Error) { setState(ConnState::Disconnected); }
    }

    bool IqClient::takeLatest(IqFrame& out) {
        std::lock_guard<std::mutex> l(boxMtx);
        if (!boxFull) { return false; }
        std::swap(out, box);
        boxFull = false;
        return true;
    }

    IqStats IqClient::stats() const {
        std::lock_guard<std::mutex> l(statsMtx);
        IqStats s = st;
        s.state = connState.load();
        return s;
    }

    void IqClient::setState(ConnState s, const std::string& msg) {
        std::lock_guard<std::mutex> l(statsMtx);
        connState = s;
        st.state = s;
        st.errorMessage = msg;
    }

    void IqClient::run() {
        while (!stopFlag) {
            setState(ConnState::Connecting);
            bool ok = session();
            if (stopFlag) { break; }
            if (!cfg.autoReconnect) { (void)ok; break; }
            // keep the Error state/message visible during the delay
            for (int waited = 0; waited < cfg.reconnectDelayMs && !stopFlag; waited += 50) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }

    // Returns false on error (state already set to Error), true on clean end.
    bool IqClient::session() {
        std::string err;
        tcp::Socket s = tcp::connectTo(cfg.host, cfg.port, cfg.connectTimeoutMs, cfg.rcvBufBytes, err);
        if (s == tcp::INVALID) {
            setState(ConnState::Error, err);
            return false;
        }
        {
            std::lock_guard<std::mutex> l(sockMtx);
            sock = s;
        }
        auto finish = [&](ConnState state, const std::string& msg) {
            {
                std::lock_guard<std::mutex> l(sockMtx);
                sock = tcp::INVALID;
                tcp::closeSocket(s);
            }
            if (stopFlag) { setState(ConnState::Disconnected); }
            else { setState(state, msg); }
            return state != ConnState::Error;
        };

        {
            std::lock_guard<std::mutex> l(statsMtx);
            haveCpi = false;
            rateT0 = std::chrono::steady_clock::now();
            rateBytes0 = st.bytesReceived;
        }

        if (!tcp::sendAll(s, "streaming", 9)) { return finish(ConnState::Error, "failed to send streaming request"); }
        setState(ConnState::AwaitingFrame);
        tcp::setRecvTimeout(s, cfg.firstFrameTimeoutMs);

        uint8_t hdr[HEADER_SIZE];
        bool gotHeader = false;
        while (!stopFlag) {
            bool timedOut = false;
            if (!tcp::recvExact(s, hdr, HEADER_SIZE, &timedOut)) {
                if (timedOut && !gotHeader) {
                    return finish(ConnState::Error, "no Heimdall frame within " + std::to_string(cfg.firstFrameTimeoutMs / 1000) +
                                                        " s; is this the DAQ IQ server? (on macOS port 5000 is taken by AirPlay Receiver)");
                }
                return finish(ConnState::Error, "connection lost while reading header");
            }
            std::string perr;
            if (!parseHeader(hdr, HEADER_SIZE, work.header, &perr, cfg.maxPayloadBytes)) {
                if (!gotHeader) { return finish(ConnState::Error, "peer is not a Heimdall IQ server (" + perr + ")"); }
                return finish(ConnState::Error, "header error: " + perr);
            }
            if (!gotHeader) {
                gotHeader = true;
                // Large CPIs can take long on slow links, so no read timeout once the peer is known
                tcp::setRecvTimeout(s, 0);
                setState(ConnState::Streaming);
            }
            uint64_t payload = work.header.payloadBytes();
            size_t nSamples = (size_t)work.header.cpiLength * work.header.activeAntChs;
            work.samples.resize(nSamples);
            if (payload > 0) {
                raw.resize((size_t)payload);
                if (!tcp::recvExact(s, raw.data(), raw.size())) {
                    return finish(ConnState::Error, "connection lost while reading payload");
                }
                // little-endian float32 pairs
                for (size_t i = 0; i < nSamples; i++) {
                    uint32_t u[2];
                    for (int k = 0; k < 2; k++) {
                        const uint8_t* b = raw.data() + i * 8 + 4 * k;
                        u[k] = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
                    }
                    float re, im;
                    std::memcpy(&re, &u[0], 4);
                    std::memcpy(&im, &u[1], 4);
                    work.samples[i] = std::complex<float>(re, im);
                }
            }
            account(work.header, HEADER_SIZE + payload);
            deliver(work);

            // pull protocol: one request per frame
            std::lock_guard<std::mutex> l(sockMtx);
            if (stopFlag) { break; }
            if (!tcp::sendAll(s, "IQDownload", 10)) { break; }
        }
        if (stopFlag) { return finish(ConnState::Disconnected, ""); }
        return finish(ConnState::Error, "failed to send IQDownload request");
    }

    void IqClient::account(const IqHeader& h, uint64_t bytes) {
        std::lock_guard<std::mutex> l(statsMtx);
        st.framesReceived++;
        if (isUsableForDoa(h)) { st.framesUsable++; }
        if (h.frameType < 5) { st.framesByType[h.frameType]++; }
        if (h.frameType != FRAME_DUMMY) {
            if (haveCpi) {
                uint32_t d = h.cpiIndex - lastCpi;
                if (d > 1 && d < 0x80000000u) { st.cpiGaps += d - 1; }
            }
            haveCpi = true;
            lastCpi = h.cpiIndex;
        }
        st.bytesReceived += bytes;
        st.lastHeader = h;
        st.haveLastHeader = true;
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - rateT0).count();
        if (dt >= 1.0) {
            st.bytesPerSecond = (double)(st.bytesReceived - rateBytes0) / dt;
            rateT0 = now;
            rateBytes0 = st.bytesReceived;
        }
    }

    void IqClient::deliver(IqFrame& f) {
        if (callback) {
            callback(f);
            return;
        }
        bool overwrote;
        {
            std::lock_guard<std::mutex> l(boxMtx);
            overwrote = boxFull;
            std::swap(box, f); // f keeps the old mailbox buffer for reuse
            boxFull = true;
        }
        if (overwrote) {
            std::lock_guard<std::mutex> l(statsMtx);
            st.framesOverwritten++;
        }
    }
}
