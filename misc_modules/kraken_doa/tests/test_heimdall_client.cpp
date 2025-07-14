// Heimdall clients against in-process fake servers on 127.0.0.1 (test fixture only).
#include <heimdall/iq_client.h>
#include <heimdall/control_client.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace kraken_doa::heimdall;
using namespace std::chrono_literals;

static std::atomic<int> failures{ 0 };
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

// ---------- socket helpers
static int listenEphemeral(int& port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(s, (sockaddr*)&a, sizeof(a)) != 0 || listen(s, 1) != 0) { std::printf("listen failed\n"); std::exit(2); }
    socklen_t l = sizeof(a);
    getsockname(s, (sockaddr*)&a, &l);
    port = ntohs(a.sin_port);
    return s;
}

// Returns bytes read; short on EOF/error.
static size_t readN(int fd, void* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, (char*)buf + got, n - got, 0);
        if (r <= 0) { break; }
        got += (size_t)r;
    }
    return got;
}

static bool writeAll(int fd, const uint8_t* p, size_t n) {
    while (n) {
        ssize_t r = send(fd, p, n, 0);
        if (r <= 0) { return false; }
        p += r; n -= (size_t)r;
    }
    return true;
}

static bool waitFor(const std::function<bool()>& cond, int ms) {
    auto t0 = std::chrono::steady_clock::now();
    while (!cond()) {
        if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(ms)) { return false; }
        std::this_thread::sleep_for(2ms);
    }
    return true;
}

// ---------- fake IQ server
static std::complex<float> sampleValue(unsigned ch, unsigned n) {
    return { (float)(ch * 1000 + n) * 0.25f - 100.0f, -(float)(ch + 2 * n) * 0.125f };
}

struct FakeFrame {
    IqHeader h;
    bool known = false;                  // payload = sampleValue(ch, n)
    std::vector<uint8_t> rawOverride;    // used when non-empty instead of the generated payload
};

static std::vector<uint8_t> encode(const FakeFrame& f) {
    std::vector<uint8_t> out(HEADER_SIZE);
    serializeHeader(f.h, out.data());
    uint64_t pb = f.h.payloadBytes();
    size_t base = out.size();
    out.resize(base + pb);
    for (unsigned ch = 0; ch < f.h.activeAntChs && pb; ch++) {
        for (unsigned n = 0; n < f.h.cpiLength; n++) {
            std::complex<float> v = f.known ? sampleValue(ch, n) : std::complex<float>(0.5f, -0.5f);
            uint8_t* d = out.data() + base + ((size_t)ch * f.h.cpiLength + n) * 8;
            uint32_t u[2];
            float fl[2] = { v.real(), v.imag() };
            std::memcpy(u, fl, 8);
            for (int k = 0; k < 2; k++) { for (int i = 0; i < 4; i++) { d[4 * k + i] = (uint8_t)(u[k] >> (8 * i)); } }
        }
    }
    return out;
}

class FakeIqServer {
public:
    explicit FakeIqServer(std::vector<std::vector<uint8_t>> frames) : frames(std::move(frames)) {
        lfd = listenEphemeral(port);
        th = std::thread([this] { serve(); });
    }
    ~FakeIqServer() {
        finish();
        close(lfd);
    }
    void finish() {
        if (th.joinable()) { th.join(); }
    }
    int port = 0;
    std::string violation; // empty if protocol followed
    bool sawQuit = false;
    std::atomic<int> requests{ 0 };

private:
    void serve() {
        // accept with a bounded wait
        timeval tv{ 10, 0 };
        setsockopt(lfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        int fd = accept(lfd, nullptr, nullptr);
        if (fd < 0) { violation = "no client connected"; return; }
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        char buf[16];
        if (readN(fd, buf, 9) != 9 || std::memcmp(buf, "streaming", 9) != 0) {
            violation = "first message was not exactly 'streaming'";
            close(fd);
            return;
        }
        std::mt19937 rng(1234);
        for (size_t i = 0; i < frames.size(); i++) {
            if (i > 0) {
                if (readN(fd, buf, 10) != 10 || std::memcmp(buf, "IQDownload", 10) != 0) {
                    violation = "frame " + std::to_string(i) + " not preceded by 'IQDownload'";
                    close(fd);
                    return;
                }
                requests++;
            }
            // split writes into small random chunks
            const auto& f = frames[i];
            size_t pos = 0;
            while (pos < f.size()) {
                size_t c = 1 + rng() % 3000;
                if (c > f.size() - pos) { c = f.size() - pos; }
                if (!writeAll(fd, f.data() + pos, c)) { close(fd); return; }
                pos += c;
                if (rng() % 8 == 0) { std::this_thread::sleep_for(1ms); }
            }
        }
        // the client asks for the next frame, then quits
        for (;;) {
            char c;
            if (readN(fd, &c, 1) != 1) { break; }
            if (c == 'q') { sawQuit = true; break; }
            if (c == 'I') {
                if (readN(fd, buf, 9) != 9 || std::memcmp(buf, "QDownload", 9) != 0) { violation = "bad trailing request"; break; }
                continue;
            }
            violation = "unexpected byte after last frame";
            break;
        }
        close(fd);
    }
    std::vector<std::vector<uint8_t>> frames;
    int lfd;
    std::thread th;
};

static FakeFrame mk(uint32_t type, uint32_t cpi, uint32_t len, bool sync, bool known) {
    FakeFrame f;
    f.h.frameType = type;
    f.h.activeAntChs = 5;
    f.h.cpiLength = len;
    f.h.cpiIndex = cpi;
    f.h.rfCenterFreq = 433920000;
    f.h.samplingFreq = 2400000;
    f.h.delaySyncFlag = sync ? 1 : 0;
    f.h.iqSyncFlag = sync ? 1 : 0;
    f.known = known;
    return f;
}

static std::vector<std::vector<uint8_t>> sequence() {
    std::vector<std::vector<uint8_t>> v;
    v.push_back(encode(mk(FRAME_DUMMY, 0, 0, false, false)));
    v.push_back(encode(mk(FRAME_CAL, 1, 512, true, false)));
    v.push_back(encode(mk(FRAME_DATA, 2, 4096, false, false)));
    v.push_back(encode(mk(FRAME_DATA, 3, 4096, true, true)));
    v.push_back(encode(mk(FRAME_DATA, 6, 4096, true, true))); // cpi 4 and 5 lost upstream
    return v;
}

static void testIqClient() {
    // ---- callback consumer: every frame delivered, payload exact
    {
        FakeIqServer srv(sequence());
        std::mutex m;
        std::vector<IqFrame> got;
        IqClient c;
        c.setFrameCallback([&](const IqFrame& f) { std::lock_guard<std::mutex> l(m); got.push_back(f); });
        IqClient::Config cfg;
        cfg.port = srv.port;
        c.start(cfg);
        CHECK(waitFor([&] { return c.stats().framesReceived == 5; }, 10000));
        CHECK(c.state() == ConnState::Streaming);
        c.stop();
        CHECK(c.state() == ConnState::Disconnected);
        IqStats s = c.stats();
        CHECK(s.framesReceived == 5);
        CHECK(s.framesUsable == 2);
        CHECK(s.framesByType[FRAME_DATA] == 3 && s.framesByType[FRAME_DUMMY] == 1 && s.framesByType[FRAME_CAL] == 1);
        CHECK(s.cpiGaps == 2);
        CHECK(s.haveLastHeader && s.lastHeader.cpiIndex == 6);
        CHECK(s.bytesReceived == (5 * 1024 + 5 * 8ull * (512 + 4096 * 3)));
        {
            std::lock_guard<std::mutex> l(m);
            CHECK(got.size() == 5);
            if (got.size() == 5) {
                CHECK(got[0].header.frameType == FRAME_DUMMY && got[0].samples.empty());
                CHECK(got[1].header.frameType == FRAME_CAL && got[1].samples.size() == 5 * 512);
                CHECK(!isUsableForDoa(got[2].header));
                for (size_t k : { (size_t)3, (size_t)4 }) {
                    const IqFrame& f = got[k];
                    CHECK(isUsableForDoa(f.header) && f.samples.size() == 5 * 4096);
                    bool exact = f.samples.size() == 5 * 4096;
                    for (unsigned ch = 0; exact && ch < 5; ch++) {
                        for (unsigned n = 0; n < 4096; n++) {
                            if (f.samples[ch * 4096 + n] != sampleValue(ch, n)) { exact = false; break; }
                        }
                    }
                    CHECK(exact);
                }
            }
        }
        srv.finish();
        CHECK(srv.violation.empty());
        if (!srv.violation.empty()) { std::printf("  server: %s\n", srv.violation.c_str()); }
        CHECK(srv.sawQuit);
        CHECK(srv.requests == 4);
    }

    // ---- mailbox consumer that never reads: only the newest frame is kept
    {
        FakeIqServer srv(sequence());
        IqClient c;
        IqClient::Config cfg;
        cfg.port = srv.port;
        c.start(cfg);
        CHECK(waitFor([&] { return c.stats().framesReceived == 5; }, 10000));
        IqFrame f;
        CHECK(c.takeLatest(f));
        CHECK(f.header.cpiIndex == 6 && f.samples.size() == 5 * 4096);
        CHECK(f.samples[4 * 4096 + 17] == sampleValue(4, 17));
        CHECK(!c.takeLatest(f));
        CHECK(c.stats().framesOverwritten == 4);
        c.stop();
        srv.finish();
        CHECK(srv.violation.empty());
    }

    // ---- bad sync word: Error state, no resync attempt
    {
        auto bad = encode(mk(FRAME_DATA, 0, 16, true, false));
        bad[0] ^= 0xff;
        FakeIqServer srv({ bad });
        IqClient c;
        IqClient::Config cfg;
        cfg.port = srv.port;
        c.start(cfg);
        CHECK(waitFor([&] { return c.state() == ConnState::Error; }, 10000));
        IqStats s = c.stats();
        CHECK(s.errorMessage.find("sync") != std::string::npos);
        CHECK(s.framesReceived == 0);
        c.stop();
        CHECK(c.state() == ConnState::Error); // error is kept, not hidden by stop()
        srv.finish();
    }

    // ---- connection refused reports Error and stop() is clean
    {
        int port;
        int l = listenEphemeral(port);
        close(l); // nothing listens here any more
        IqClient c;
        IqClient::Config cfg;
        cfg.port = port;
        c.start(cfg);
        CHECK(waitFor([&] { return c.state() == ConnState::Error; }, 10000));
        c.stop();
    }

    // ---- stop() while blocked waiting for data (server never answers the first request)
    {
        int port;
        int l = listenEphemeral(port);
        std::thread t([&] {
            int fd = accept(l, nullptr, nullptr);
            char b[16];
            readN(fd, b, 9);
            char c;
            readN(fd, &c, 1); // waits for 'q' (client stop) or EOF
            close(fd);
        });
        IqClient c;
        IqClient::Config cfg;
        cfg.port = port;
        c.start(cfg);
        CHECK(waitFor([&] { return c.state() == ConnState::AwaitingFrame; }, 5000));
        std::this_thread::sleep_for(50ms);
        auto t0 = std::chrono::steady_clock::now();
        c.stop();
        CHECK(std::chrono::steady_clock::now() - t0 < 2s);
        CHECK(c.state() == ConnState::Disconnected);
        t.join();
        close(l);
    }
    // ---- a TCP peer that accepts but never sends a frame is reported, not shown as streaming
    {
        int port;
        int l = listenEphemeral(port);
        std::thread t([&] {
            int fd = accept(l, nullptr, nullptr);
            char b[16];
            readN(fd, b, 9);
            char c;
            readN(fd, &c, 1); // EOF when the client gives up
            close(fd);
        });
        IqClient c;
        IqClient::Config cfg;
        cfg.port = port;
        cfg.firstFrameTimeoutMs = 500;
        c.start(cfg);
        CHECK(waitFor([&] { return c.state() == ConnState::Error; }, 5000));
        CHECK(c.stats().errorMessage.find("no Heimdall frame") != std::string::npos);
        c.stop();
        t.join();
        close(l);
    }

    // ---- a peer that answers with something else (e.g. an HTTP server on port 5000)
    {
        int port;
        int l = listenEphemeral(port);
        std::thread t([&] {
            int fd = accept(l, nullptr, nullptr);
            char b[16];
            readN(fd, b, 9);
            std::vector<char> resp(1024, ' ');
            const char* http = "HTTP/1.1 403 Forbidden\r\nServer: AirTunes\r\n\r\n";
            std::memcpy(resp.data(), http, std::strlen(http));
            send(fd, resp.data(), resp.size(), 0);
            char c;
            readN(fd, &c, 1);
            close(fd);
        });
        IqClient c;
        IqClient::Config cfg;
        cfg.port = port;
        c.start(cfg);
        CHECK(waitFor([&] { return c.state() == ConnState::Error; }, 5000));
        CHECK(c.stats().errorMessage.find("not a Heimdall IQ server") != std::string::npos);
        CHECK(c.stats().framesReceived == 0);
        c.stop();
        t.join();
        close(l);
    }
    std::printf("  iq client tests done\n");
}

// ---------- fake control server
class FakeControlServer {
public:
    FakeControlServer() {
        lfd = listenEphemeral(port);
        th = std::thread([this] { serve(); });
    }
    ~FakeControlServer() {
        if (th.joinable()) { th.join(); }
        close(lfd);
    }
    void finish() { if (th.joinable()) { th.join(); } }
    int port = 0;
    std::mutex m;
    std::vector<std::vector<uint8_t>> msgs;
    std::string violation;
    int silentIndex = -1;           // message index that gets no reply
    int badReplyIndex = -1;         // message index answered with something other than FNSD
    bool sawExit = false;

private:
    void serve() {
        timeval tv{ 10, 0 };
        setsockopt(lfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        int fd = accept(lfd, nullptr, nullptr);
        if (fd < 0) { violation = "no client"; return; }
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        for (int idx = 0;; idx++) {
            std::vector<uint8_t> msg(128);
            size_t n = readN(fd, msg.data(), 128);
            if (n == 0) { break; }
            if (n != 128) { violation = "short control message of " + std::to_string(n) + " bytes"; break; }
            {
                std::lock_guard<std::mutex> l(m);
                msgs.push_back(msg);
            }
            if (std::memcmp(msg.data(), "EXIT", 4) == 0) { sawExit = true; break; }
            if (idx == silentIndex) { continue; }
            uint8_t reply[128] = { 0 };
            std::memcpy(reply, idx == badReplyIndex ? "NOPE" : "FNSD", 4);
            if (!writeAll(fd, reply, 128)) { break; }
        }
        close(fd);
    }
    int lfd;
    std::thread th;
};

static bool allZeroFrom(const std::vector<uint8_t>& m, size_t from) {
    for (size_t i = from; i < m.size(); i++) { if (m[i]) { return false; } }
    return true;
}

static uint32_t le32(const std::vector<uint8_t>& m, size_t o) {
    return (uint32_t)m[o] | ((uint32_t)m[o + 1] << 8) | ((uint32_t)m[o + 2] << 16) | ((uint32_t)m[o + 3] << 24);
}

static void testControlClient() {
    FakeControlServer srv;
    srv.badReplyIndex = 4;
    srv.silentIndex = 5;
    ControlClient c;
    c.setReplyTimeoutMs(400);
    CHECK(!c.setCenterFrequency(1)); // not connected
    CHECK(c.connect("127.0.0.1", srv.port));
    CHECK(c.isConnected());

    CHECK(c.init());                                      // 0
    CHECK(c.setCenterFrequency(0x0102030405060708ull));   // 1
    CHECK(c.setGains({ 0.3f, 9.1f, 100.0f, 22.9f, 13.0f }, 5)); // 2
    CHECK(c.setAgc());                                    // 3
    CHECK(!c.setAgc());                                   // 4: reply without FNSD
    CHECK(c.lastError().find("FNSD") != std::string::npos);
    CHECK(c.isConnected());                               // stream still in step, stays open
    // argument checks never reach the wire
    CHECK(!c.setGains({ 1.0f, 2.0f }, 5));
    CHECK(!c.setGains({ 1.0f }, 0));
    CHECK(!c.setGains({ 1.0f }, 32));

    auto t0 = std::chrono::steady_clock::now();
    CHECK(!c.setCenterFrequency(433920000));              // 5: no reply -> timeout
    auto dt = std::chrono::steady_clock::now() - t0;
    CHECK(dt >= 350ms && dt < 3s);
    CHECK(c.lastError().find("timed out") != std::string::npos);
    CHECK(!c.isConnected());                              // desynced stream is dropped
    c.disconnect();
    srv.finish();

    CHECK(srv.violation.empty());
    if (!srv.violation.empty()) { std::printf("  server: %s\n", srv.violation.c_str()); }
    std::lock_guard<std::mutex> l(srv.m);
    CHECK(srv.msgs.size() == 6);
    if (srv.msgs.size() == 6) {
        CHECK(std::memcmp(srv.msgs[0].data(), "INIT", 4) == 0 && allZeroFrom(srv.msgs[0], 4));
        const auto& f = srv.msgs[1];
        CHECK(std::memcmp(f.data(), "FREQ", 4) == 0);
        const uint8_t hz[8] = { 8, 7, 6, 5, 4, 3, 2, 1 };
        CHECK(std::memcmp(f.data() + 4, hz, 8) == 0 && allZeroFrom(f, 12));
        const auto& g = srv.msgs[2];
        CHECK(std::memcmp(g.data(), "GAIN", 4) == 0);
        CHECK(le32(g, 4) == 0 && le32(g, 8) == 87 && le32(g, 12) == 496 && le32(g, 16) == 229 && le32(g, 20) == 125);
        CHECK(allZeroFrom(g, 24));
        CHECK(std::memcmp(srv.msgs[3].data(), "AGC ", 4) == 0 && allZeroFrom(srv.msgs[3], 4));
        const auto& q = srv.msgs[5];
        CHECK(std::memcmp(q.data(), "FREQ", 4) == 0 && le32(q, 4) == 433920000u);
    }

    // clean disconnect sends EXIT as a full 128 byte message
    {
        FakeControlServer s2;
        ControlClient c2;
        CHECK(c2.connect("127.0.0.1", s2.port));
        CHECK(c2.init());
        c2.disconnect();
        CHECK(!c2.isConnected());
        s2.finish();
        CHECK(s2.violation.empty() && s2.sawExit);
        std::lock_guard<std::mutex> l2(s2.m);
        CHECK(s2.msgs.size() == 2 && std::memcmp(s2.msgs[1].data(), "EXIT", 4) == 0 && allZeroFrom(s2.msgs[1], 4));
    }

    // gain list helpers
    CHECK(ControlClient::acceptedGainsTenthDb().size() == 29);
    CHECK(ControlClient::acceptedGainsDb().back() == 49.6f);
    CHECK(ControlClient::snapGainTenthDb(-5.0f) == 0 && ControlClient::snapGainTenthDb(60.0f) == 496);
    CHECK(ControlClient::snapGainTenthDb(14.4f) == 144);
    std::printf("  control client tests done\n");

    // connection failure
    {
        int port;
        int l3 = listenEphemeral(port);
        close(l3);
        ControlClient c3;
        CHECK(!c3.connect("127.0.0.1", port, 1000));
        CHECK(!c3.lastError().empty());
    }
}

int main() {
    testIqClient();
    testControlClient();
    std::printf(failures ? "FAILED (%d)\n" : "test_kraken_heimdall_client OK\n", failures.load());
    return failures ? 1 : 0;
}
