#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <random>
#include <vector>
#include "dsp/array_geometry.h"
#include "dsp/channelizer.h"
#include "dsp/doa_estimator.h"

using namespace kraken_doa::dsp;
using cf = std::complex<float>;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double angDiff(double a, double b) {
    double d = std::fmod(a - b + 540.0, 360.0) - 180.0;
    return std::fabs(d);
}

// Plane waves from independently written steering model; positions in wavelengths
static std::vector<cf> synth(const std::vector<double>& x, const std::vector<double>& y,
                             const std::vector<double>& thetaDeg, double snrDb, size_t N, unsigned seed) {
    const int M = (int)x.size();
    std::mt19937 rng(seed);
    std::normal_distribution<double> g(0.0, std::sqrt(0.5));
    std::vector<cf> d((size_t)M * N);
    const double sigma2 = std::pow(10.0, -snrDb / 10.0);
    for (size_t n = 0; n < N; n++) {
        std::vector<std::complex<double>> s;
        for (size_t k = 0; k < thetaDeg.size(); k++) { s.emplace_back(g(rng), g(rng)); }
        for (int m = 0; m < M; m++) {
            std::complex<double> v(0, 0);
            for (size_t k = 0; k < thetaDeg.size(); k++) {
                const double t = thetaDeg[k] * M_PI / 180.0;
                const double ph = 2.0 * M_PI * (x[m] * std::cos(t) + y[m] * std::sin(t));
                v += s[k] * std::complex<double>(std::cos(ph), std::sin(ph));
            }
            v += std::sqrt(sigma2) * std::complex<double>(g(rng), g(rng));
            d[(size_t)m * N + n] = cf((float)v.real(), (float)v.imag());
        }
    }
    return d;
}

static const char* algName(DoaAlgorithm a) {
    return a == DoaAlgorithm::Bartlett ? "Bartlett" : a == DoaAlgorithm::Capon ? "Capon" : "MUSIC";
}

static void testDoa() {
    printf("DOA\n");
    const double fc = 433e6, lambda = SPEED_OF_LIGHT / fc;
    const size_t N = 8192;
    const DoaAlgorithm algs[] = { DoaAlgorithm::Bartlett, DoaAlgorithm::Capon, DoaAlgorithm::Music };

    // Independent geometry check against the stated formulas
    {
        ArrayGeometry g{ ArrayType::UCA, 5, 0.5 * lambda };
        std::vector<double> x, y;
        g.positions(fc, x, y);
        CHECK(std::fabs(x[1] - 0.5 * std::cos(2 * M_PI / 5)) < 1e-12 && std::fabs(y[1] + 0.5 * std::sin(2 * M_PI / 5)) < 1e-12, "UCA positions");
        ArrayGeometry u{ ArrayType::ULA, 5, lambda / 2 };
        u.positions(fc, x, y);
        CHECK(x[3] == 0.0 && std::fabs(y[3] + 1.5) < 1e-12, "ULA positions");
    }

    // UCA
    {
        ArrayGeometry g{ ArrayType::UCA, 5, 0.5 * lambda };
        std::vector<double> x, y;
        g.positions(fc, x, y);
        const double truths[] = { 0, 45, 137, 270, 359 };
        for (DoaAlgorithm a : algs) {
            DoaConfig cfg;
            cfg.algorithm = a;
            DoaEstimator est;
            CHECK(est.configure(x, y, cfg), "configure");
            double worst = 0;
            printf("  UCA %-8s err(deg):", algName(a));
            for (double t : truths) {
                auto d = synth(x, y, { t }, 10.0, N, 1234 + (unsigned)t);
                DoaResult r;
                CHECK(est.process(d.data(), N, r), "process");
                const double e = angDiff(r.peakThetaDeg, t);
                printf(" %.0f:%.1f", t, e);
                worst = std::max(worst, e);
                CHECK(e <= 1.0, "UCA %s truth %.0f got %.0f", algName(a), t, r.peakThetaDeg);
            }
            printf("  worst %.1f\n", worst);
        }

        // Two sources, K=2
        DoaConfig cfg;
        cfg.algorithm = DoaAlgorithm::Music;
        cfg.numSources = 2;
        DoaEstimator est;
        est.configure(x, y, cfg);
        auto d = synth(x, y, { 100, 160 }, 15.0, N, 99);
        DoaResult r;
        est.process(d.data(), N, r);
        auto sp = [&](double t) { return r.spectrumDb[(size_t)t]; };
        auto isPeak = [&](double t) { return sp(t) >= sp(t - 3) && sp(t) >= sp(t + 3) && sp(t) > -10.0; };
        double best100 = -1e9, best160 = -1e9;
        int p100 = 0, p160 = 0;
        for (int t = 90; t <= 110; t++) { if (sp(t) > best100) { best100 = sp(t); p100 = t; } }
        for (int t = 150; t <= 170; t++) { if (sp(t) > best160) { best160 = sp(t); p160 = t; } }
        printf("  UCA MUSIC K=2 peaks near %d (%.1f dB), %d (%.1f dB), eig:", p100, best100, p160, best160);
        for (double e : r.eigenvalues) { printf(" %.3g", e); }
        printf("\n");
        CHECK(isPeak(p100) && isPeak(p160), "two sources not resolved");
        CHECK(angDiff(p100, 100) <= 2 && angDiff(p160, 160) <= 2, "two-source angles %d %d", p100, p160);
    }

    // ULA
    {
        ArrayGeometry g{ ArrayType::ULA, 5, lambda / 2 };
        std::vector<double> x, y;
        g.positions(fc, x, y);
        const double truths[] = { 0, 20, 45, 300, 359 }; // all with cos >= 0 (forward)
        for (DoaAlgorithm a : algs) {
            DoaConfig cfg;
            cfg.algorithm = a;
            cfg.half = ScanHalf::Forward;
            DoaEstimator est;
            est.configure(x, y, cfg);
            printf("  ULA %-8s err(deg):", algName(a));
            for (double t : truths) {
                auto d = synth(x, y, { t }, 10.0, N, 777 + (unsigned)t);
                DoaResult r;
                est.process(d.data(), N, r);
                const double e = angDiff(r.peakThetaDeg, t);
                printf(" %.0f:%.1f", t, e);
                CHECK(e <= 1.0, "ULA %s truth %.0f got %.0f", algName(a), t, r.peakThetaDeg);
            }
            printf("\n");
        }
        // Backward half sees the mirror image
        {
            DoaConfig cfg;
            cfg.half = ScanHalf::Backward;
            DoaEstimator est;
            est.configure(x, y, cfg);
            auto d = synth(x, y, { 45 }, 10.0, N, 5);
            DoaResult r;
            est.process(d.data(), N, r);
            printf("  ULA MUSIC Backward truth 45 -> %.0f (mirror 135)\n", r.peakThetaDeg);
            CHECK(angDiff(r.peakThetaDeg, 135) <= 1.0, "backward mirror");
        }
        // Both: mirror ambiguity
        DoaConfig cfg;
        cfg.algorithm = DoaAlgorithm::Music;
        cfg.half = ScanHalf::Both;
        DoaEstimator est;
        est.configure(x, y, cfg);
        auto d = synth(x, y, { 45 }, 10.0, N, 4);
        DoaResult r;
        est.process(d.data(), N, r);
        const float a = r.spectrumDb[45], b = r.spectrumDb[135];
        printf("  ULA Both truth 45: spectrum at 45 = %.2f dB, at mirror 135 = %.2f dB\n", a, b);
        CHECK(std::fabs(a - b) < 0.5f && a > -3.0f && b > -3.0f, "mirror ambiguity not shown");
    }

    // Bearing conversion
    CHECK(bearingFromTheta(0, 0) == 0.0, "bearing 0");
    CHECK(bearingFromTheta(45, 0) == 315.0, "bearing 45");        // 360-45
    CHECK(bearingFromTheta(270, 30) == 120.0, "bearing 270+30");  // 360-270+30
    CHECK(bearingFromTheta(10, 20) == 10.0, "bearing wrap");      // 370 mod 360
    CHECK(bearingFromTheta(350, 0) == 10.0, "bearing 350");
    printf("  bearing conversion checked\n");
}

static void testChannelizer() {
    printf("Channelizer\n");
    const double fs = 1e6, bw = 10e3, fWanted = 50e3, fOther = 80e3;
    const size_t N = 262144;
    const int M = 5;
    const double phase[M] = { 0.0, 0.7, -1.9, 2.5, -0.4 }; // rad
    std::vector<cf> d((size_t)M * N);
    for (int m = 0; m < M; m++) {
        for (size_t n = 0; n < N; n++) {
            const double a = 2 * M_PI * fWanted * n / fs + phase[m];
            const double b = 2 * M_PI * fOther * n / fs + 0.3 * m;
            d[(size_t)m * N + n] = cf((float)(0.5 * std::cos(a) + 0.5 * std::cos(b)), (float)(0.5 * std::sin(a) + 0.5 * std::sin(b)));
        }
    }
    Channelizer ch;
    ch.configure({ fs, fWanted, bw });
    CHECK(ch.process(d.data(), M, N), "process");
    const size_t L = ch.outputLength();
    printf("  decimation %d, taps %d, output %zu samples at %.0f Hz\n", ch.decimation(), ch.tapCount(), L, ch.outputRate());
    CHECK(L == N / ch.decimation(), "output length");

    const size_t skip = (size_t)ch.tapCount() / ch.decimation() + 2;
    std::complex<double> mean[M];
    double resid = 0, wanted = 0;
    for (int m = 0; m < M; m++) {
        const cf* y = ch.output() + (size_t)m * L;
        std::complex<double> s(0, 0);
        for (size_t n = skip; n < L - skip; n++) { s += std::complex<double>(y[n]); }
        mean[m] = s / (double)(L - 2 * skip);
        double r = 0;
        for (size_t n = skip; n < L - skip; n++) { r += std::norm(std::complex<double>(y[n]) - mean[m]); }
        if (m == 0) { resid = r / (L - 2 * skip); wanted = std::norm(mean[0]); }
    }
    const double atten = 10 * std::log10(wanted / resid);
    printf("  unwanted tone (0.5 amplitude, %.0f kHz away) attenuation vs wanted: %.1f dB\n", (fOther - fWanted) / 1e3, atten);
    CHECK(atten > 40.0, "attenuation %.1f dB", atten);

    double worstPhase = 0;
    for (int m = 1; m < M; m++) {
        const double meas = std::arg(mean[m] * std::conj(mean[0])) * 180 / M_PI;
        const double exp = (phase[m] - phase[0]) * 180 / M_PI;
        worstPhase = std::max(worstPhase, angDiff(meas, exp));
    }
    printf("  worst inter-channel phase error: %.4f deg\n", worstPhase);
    CHECK(worstPhase < 0.5, "phase error %.3f", worstPhase);
    printf("  power %.2f dBFS (expect %.2f)\n", ch.powerDbfs(), 10 * std::log10(0.25));

    // Bypass
    Channelizer by;
    by.configure({ fs, 0.0, fs });
    by.process(d.data(), M, N);
    CHECK(by.bypassed() && by.outputLength() == N && by.output()[5] == d[5], "bypass");

    // Benchmark
    const size_t NB = 1048576;
    std::vector<cf> big((size_t)M * NB, cf(0.1f, -0.2f));
    for (size_t i = 0; i < big.size(); i += 7) { big[i] = cf(0.5f, 0.5f); }
    Channelizer cb;
    cb.configure({ 2.4e6, 300e3, 12.5e3 });
    auto t0 = std::chrono::steady_clock::now();
    cb.process(big.data(), M, NB);
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("  benchmark 1048576 x 5 (fs 2.4M, bw 12.5k, %d taps, decim %d): %.1f ms\n", cb.tapCount(), cb.decimation(), sec * 1e3);
    CHECK(sec < 2.0, "benchmark too slow");
}

int main() {
    testDoa();
    testChannelizer();
    printf(failures ? "FAILED (%d)\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
