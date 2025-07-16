// DoaPipeline against an in-process fake Heimdall server on 127.0.0.1 (test fixture only).
// The plane wave is generated here with the steering formula written independently of src/dsp.
#include <pipeline.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace kraken_doa;
using namespace kraken_doa::heimdall;
using namespace std::chrono_literals;

static std::atomic<int> failures{ 0 };
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static const double FC = 433.92e6;
static const double FS = 240000.0;
static const double TONE_OFFSET = 20000.0; // channel offset set in the pipeline
static const double TONE_DELTA = 1000.0;   // tone sits 1 kHz above the offset, inside the band
static const double RADIUS_M = 0.17;
static const unsigned CPI = 24000;

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
        p += r;
        n -= (size_t)r;
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

static std::vector<uint8_t> encode(const IqHeader& h, const std::vector<std::complex<float>>& samples) {
    std::vector<uint8_t> out(HEADER_SIZE);
    serializeHeader(h, out.data());
    size_t base = out.size();
    out.resize(base + samples.size() * 8);
    for (size_t i = 0; i < samples.size(); i++) {
        float fl[2] = { samples[i].real(), samples[i].imag() };
        uint32_t u[2];
        std::memcpy(u, fl, 8);
        for (int k = 0; k < 2; k++) {
            for (int b = 0; b < 4; b++) { out[base + i * 8 + 4 * k + b] = (uint8_t)(u[k] >> (8 * b)); }
        }
    }
    return out;
}

static std::atomic<double> g_amp{ 0.3 }; // plane wave amplitude
static double g_truthBearing = 137.0;     // reference convention (compass offset 0)

// 5-element UCA, channel-major. Steering written from the formula: element m at
// (r cos(2 pi m/M), -r sin(2 pi m/M)) wavelengths, plane wave from angle theta,
// phase 2 pi (x cos theta + y sin theta). Reference bearing = (360 - theta) mod 360.
static std::vector<std::complex<float>> planeWave(unsigned frameIdx, double amp, double bearingDeg) {
    const int M = 5;
    const double theta = std::fmod(360.0 - bearingDeg, 360.0) * M_PI / 180.0;
    const double f = FC + TONE_OFFSET + TONE_DELTA;
    const double r = RADIUS_M * f / 299792458.0;
    std::mt19937 rng(1000 + frameIdx);
    std::normal_distribution<double> g(0.0, 0.02 / std::sqrt(2.0)); // AWGN, 0.02 rms per complex sample
    std::vector<std::complex<float>> d((size_t)M * CPI);
    const double w = 2.0 * M_PI * (TONE_OFFSET + TONE_DELTA) / FS;
    for (int m = 0; m < M; m++) {
        const double x = r * std::cos(2.0 * M_PI * m / M);
        const double y = -r * std::sin(2.0 * M_PI * m / M);
        const double ph = 2.0 * M_PI * (x * std::cos(theta) + y * std::sin(theta));
        for (unsigned n = 0; n < CPI; n++) {
            double a = w * n + ph + 0.7 * frameIdx;
            d[(size_t)m * CPI + n] = { (float)(amp * std::cos(a) + g(rng)), (float)(amp * std::sin(a) + g(rng)) };
        }
    }
    return d;
}

static IqHeader baseHeader(uint32_t type, uint32_t cpi, uint32_t len) {
    IqHeader h;
    h.frameType = type;
    std::strcpy(h.hardwareId, "kraken5");
    h.activeAntChs = 5;
    h.cpiLength = len;
    h.cpiIndex = cpi;
    h.rfCenterFreq = (uint64_t)FC;
    h.samplingFreq = (uint64_t)FS;
    h.adcSamplingFreq = 2400000;
    h.timeStamp = 1700000000000ull + cpi;
    return h;
}

// Frame sequence: 0 CAL, 1 DATA INIT-ish unsynced (fractional cal), 2 DATA with delay sync only, 3.. synced.
static std::vector<uint8_t> makeFrame(unsigned i) {
    if (i == 0) {
        IqHeader h = baseHeader(FRAME_CAL, i, 512);
        h.syncState = 3;
        h.noiseSourceState = 1;
        return encode(h, std::vector<std::complex<float>>(5 * 512, { 0.1f, 0.0f }));
    }
    IqHeader h = baseHeader(FRAME_DATA, i, CPI);
    if (i == 1) {
        h.syncState = 3;
    }
    else if (i == 2) {
        h.syncState = 4;
        h.delaySyncFlag = 1;
    }
    else {
        h.syncState = 6;
        h.delaySyncFlag = 1;
        h.iqSyncFlag = 1;
        return encode(h, planeWave(i, g_amp.load(), g_truthBearing));
    }
    return encode(h, planeWave(i, g_amp.load(), g_truthBearing)); // unsynced frames still carry data, must not be used
}

class FakeServer {
public:
    FakeServer() {
        lfd = listenEphemeral(port);
        th = std::thread([this] { serve(); });
    }
    ~FakeServer() {
        stop = true;
        if (th.joinable()) { th.join(); }
        close(lfd);
    }
    void allow(unsigned n) { allowed = n; }
    int port = 0;
    std::atomic<unsigned> allowed{ 0 };
    std::string violation;

private:
    void serve() {
        timeval tv{ 10, 0 };
        setsockopt(lfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        int fd = accept(lfd, nullptr, nullptr);
        if (fd < 0) { violation = "no client"; return; }
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        char buf[16];
        if (readN(fd, buf, 9) != 9 || std::memcmp(buf, "streaming", 9) != 0) {
            violation = "bad hello";
            close(fd);
            return;
        }
        for (unsigned i = 0; !stop; i++) {
            while (!stop && allowed.load() <= i) { std::this_thread::sleep_for(1ms); }
            if (stop) { break; }
            auto f = makeFrame(i);
            if (!writeAll(fd, f.data(), f.size())) { break; }
            if (readN(fd, buf, 10) != 10 || std::memcmp(buf, "IQDownload", 10) != 0) { break; } // 'q' or EOF ends
        }
        close(fd);
    }
    int lfd;
    std::thread th;
    std::atomic<bool> stop{ false };
};

static double angDiff(double a, double b) {
    return std::fabs(std::fmod(a - b + 540.0, 360.0) - 180.0);
}

static DoaSettings baseSettings(int elements) {
    DoaSettings s;
    s.offsetHz = TONE_OFFSET;
    s.bandwidthHz = 12500.0;
    s.squelchDbfs = -40.0;
    s.arrayType = dsp::ArrayType::UCA;
    s.elements = elements;
    s.sizeMeters = RADIUS_M;
    s.algorithm = dsp::DoaAlgorithm::Music;
    return s;
}

static bool contains(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

static void testBearingAndSkips() {
    std::printf("skip reasons, bearing, squelch\n");
    g_amp = 0.3;
    g_truthBearing = 137.0;
    FakeServer srv;
    DoaPipeline p;
    p.setSettings(baseSettings(5));
    p.start("127.0.0.1", srv.port);

    srv.allow(1); // CAL
    CHECK(waitFor([&] { return p.processedCount() >= 1; }, 5000));
    PipelineStatus st = p.status();
    CHECK(st.skipKind == SkipKind::NotDataFrame);
    CHECK(contains(st.skipReason, "CAL"));
    CHECK(!st.haveResult);

    srv.allow(2); // unsynced DATA, fractional cal
    CHECK(waitFor([&] { return p.processedCount() >= 2; }, 5000));
    st = p.status();
    CHECK(st.skipKind == SkipKind::NotSynced);
    CHECK(contains(st.skipReason, "fractional cal"));
    CHECK(contains(st.skipReason, "delay_sync 0"));
    CHECK(!st.haveResult);

    srv.allow(3); // delay synced only
    CHECK(waitFor([&] { return p.processedCount() >= 3; }, 5000));
    st = p.status();
    CHECK(st.skipKind == SkipKind::NotSynced);
    CHECK(contains(st.skipReason, "IQ cal"));
    CHECK(contains(st.skipReason, "iq_sync 0"));
    CHECK(!st.haveResult);
    CHECK(st.history.empty());

    srv.allow(100000); // synced frames from here on
    CHECK(waitFor([&] { return p.status().haveResult; }, 10000));
    st = p.status();
    if (st.haveResult) {
        std::printf("  bearing %.1f (truth %.1f), quality %.1f dB, power %.1f dBFS, %.1f ms/frame\n", st.last.doa.bearingDeg,
                    g_truthBearing, st.last.doa.peakQualityDb, st.last.powerDbfs, st.processingMs);
        CHECK(angDiff(st.last.doa.bearingDeg, g_truthBearing) < 2.0);
        CHECK(st.last.cpiIndex >= 3);
        CHECK(st.last.headerTimeStamp == 1700000000000ull + st.last.cpiIndex);
        CHECK(st.last.centerFreqHz == (uint64_t)FC);
        CHECK(st.last.sampleRateHz == (uint64_t)FS);
        CHECK(st.last.algorithm == dsp::DoaAlgorithm::Music);
        CHECK(st.last.doa.spectrumDb.size() == 360);
        CHECK(!st.history.empty());
        CHECK(st.skipKind == SkipKind::None || st.skipKind == SkipKind::BelowSquelch);
    }
    CHECK(st.iq.framesByType[FRAME_CAL] >= 1);
    CHECK(st.iq.framesUsable >= 1);

    // Reduced amplitude: below squelch
    g_amp = 0.001;
    CHECK(waitFor([&] { return p.status().skipKind == SkipKind::BelowSquelch; }, 10000));
    st = p.status();
    std::printf("  skip: %s\n", st.skipReason.c_str());
    CHECK(contains(st.skipReason, "below squelch"));
    CHECK(contains(st.skipReason, "dBFS < -40.0 dBFS"));
    CHECK(st.havePower && st.channelPowerDbfs < -40.0f);
    // the earlier real result is kept (the UI decides about staleness), never replaced by a fake one
    CHECK(st.haveResult);

    p.stop();
    CHECK(srv.violation.empty());
}

static void testChannelMismatch() {
    std::printf("channel count mismatch\n");
    g_amp = 0.3;
    FakeServer srv;
    DoaPipeline p;
    p.setSettings(baseSettings(4));
    p.start("127.0.0.1", srv.port);
    srv.allow(100000);
    CHECK(waitFor([&] { return p.status().skipKind == SkipKind::ChannelMismatch; }, 10000));
    PipelineStatus st = p.status();
    std::printf("  skip: %s\n", st.skipReason.c_str());
    CHECK(st.skipReason == "array has 4 elements, DAQ sends 5 channels");
    CHECK(!st.haveResult);
    CHECK(st.history.empty());
    p.stop();
}

int main() {
    testBearingAndSkips();
    testChannelMismatch();
    if (failures) {
        std::printf("%d failure(s)\n", failures.load());
        return 1;
    }
    std::printf("all pipeline tests passed\n");
    return 0;
}
