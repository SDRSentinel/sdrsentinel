#include <iostream>
#include <cmath>
#include <complex>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "vna/vna_analyzer.h"
#include "vna/trace_import.h"
#include "vna/scalar_sweep.h"

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\n[TEST FAILED] " << msg << " (" #cond " at " << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

using namespace rf_tools::vna;
namespace fs = std::filesystem;

static fs::path g_dir;

static std::string writeFile(const std::string& name, const std::string& content) {
    fs::path p = g_dir / name;
    std::ofstream f(p, std::ios::binary);
    f << content;
    return p.string();
}

static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// ---------------------------------------------------------------------------------------------
// Fake receiver: a known thru level (with a ripple the normalization must cancel), a known notch
// response for the DUT and a known noise floor. Test fixture only.
// ---------------------------------------------------------------------------------------------
struct FakeProbe : ScalarProbe {
    SweepStage mode = SweepStage::Thru;
    double f0 = 100.0e6, q = 2000.0;
    double depthDb = 20.0;
    double floorDb = -80.0;

    double lastTune = 0.0;
    int tunes = 0, measures = 0, finishes = 0;
    std::vector<double> tuned;
    bool failTune = false;
    int cancelAtMeasure = -1; // call sweep->cancel() inside this measurement (1-based)
    ScalarSweep* sweep = nullptr;

    static double thruDb(double f) { return -30.0 + 2.0 * std::sin(f * 2.0e-5); }

    double notchDb(double f) const {
        double bw = f0 / q;
        double minLin = std::pow(10.0, -depthDb / 20.0);
        std::complex<double> s21 = 1.0 - (1.0 - minLin) / std::complex<double>(1.0, (f - f0) / (0.5 * bw));
        return 20.0 * std::log10(std::abs(s21));
    }

    bool tune(double hz) override {
        if (failTune) { return false; }
        lastTune = hz;
        tunes++;
        tuned.push_back(hz);
        return true;
    }

    bool measurePowerDb(double, double, float& out) override {
        measures++;
        if (measures == cancelAtMeasure && sweep) { sweep->cancel(); }
        double floorLin = std::pow(10.0, floorDb / 10.0);
        double db;
        if (mode == SweepStage::Noise) { out = (float)floorDb; return true; }
        db = thruDb(lastTune) + (mode == SweepStage::Dut ? notchDb(lastTune) : 0.0);
        out = (float)(10.0 * std::log10(std::pow(10.0, db / 10.0) + floorLin)); // noise adds in power
        return true;
    }

    void finish() override { finishes++; }
    std::string lastError() const override { return failTune ? "fake tune failure" : ""; }
};

// Blocks in measurePowerDb until requestCancel(), like a stalled stream
struct BlockingProbe : ScalarProbe {
    std::mutex m;
    std::condition_variable cv;
    bool cancelled = false, entered = false;
    std::atomic<int>* finishes = nullptr;
    bool tune(double) override { return true; }
    bool measurePowerDb(double, double, float&) override {
        std::unique_lock<std::mutex> l(m);
        entered = true;
        cv.notify_all();
        cv.wait(l, [this] { return cancelled; });
        return false;
    }
    void requestCancel() override {
        std::lock_guard<std::mutex> l(m);
        cancelled = true;
        cv.notify_all();
    }
    void finish() override { if (finishes) { (*finishes)++; } }
};

static ScalarSweepSettings notchSettings() {
    ScalarSweepSettings s;
    double bw = 100.0e6 / 2000.0;
    s.startHz = 100.0e6 - 4.0 * bw;
    s.stopHz = 100.0e6 + 4.0 * bw;
    s.points = 201;
    return s;
}

int main() {
    g_dir = fs::temp_directory_path() / "rf_tools_resonator_test";
    fs::remove_all(g_dir);
    fs::create_directories(g_dir);

    std::cout << "============================================================\n";
    std::cout << "   rf_tools resonator import and scalar sweep tests         \n";
    std::cout << "============================================================\n";

    // S11, S12 and S22 are filled with values that would fail the checks if picked by mistake
    const char* junk = "9 9";

    // --------------------------------------------------------------------------------------
    std::cout << "[TEST] 1. Touchstone .s2p: RI / MA / DB and Hz / MHz / GHz\n";
    {
        // S21 = 0.3 + 0.4j -> |S21| = 0.5 = -6.0206 dB, 53.1301 deg
        std::string ri = std::string("! comment\n# GHz S RI R 50\n")
            + "0.1 " + junk + " 0.3 0.4 " + junk + " " + junk + "\n"
            + "0.2 " + junk + " 0 -0.1 " + junk + " " + junk + "   ! trailing comment\n";
        auto r = parseTouchstoneS2P(ri);
        TEST_CHECK(r.ok, "RI/GHz must parse: " + r.error);
        TEST_CHECK(r.trace.size() == 2, "two records");
        TEST_CHECK(near(r.trace[0].freqHz, 1.0e8, 1e-3) && near(r.trace[1].freqHz, 2.0e8, 1e-3), "GHz scaling");
        TEST_CHECK(near(r.trace[0].magnitudeDb, -6.0206, 1e-3), "RI magnitude dB");
        TEST_CHECK(near(r.trace[0].phaseDeg, 53.1301, 1e-3), "RI phase deg");
        TEST_CHECK(near(r.trace[1].magnitudeDb, -20.0, 1e-4), "RI second record magnitude (0 - 0.1j = -20 dB)");
        TEST_CHECK(near(r.trace[1].phaseDeg, -90.0, 1e-3), "RI second record phase");

        // MA in MHz, upper case unit: 0.1 at -45 deg -> -20 dB
        std::string ma = std::string("# MHz S MA R 50\n")
            + "100 " + junk + " 0.1 -45 " + junk + " " + junk + "\n"
            + "101 " + junk + " 1.0 10 " + junk + " " + junk + "\n";
        r = parseTouchstoneS2P(ma);
        TEST_CHECK(r.ok, "MA/MHz must parse: " + r.error);
        TEST_CHECK(near(r.trace[0].freqHz, 1.0e8, 1e-3) && near(r.trace[1].freqHz, 1.01e8, 1e-3), "MHz scaling");
        TEST_CHECK(near(r.trace[0].magnitudeDb, -20.0, 1e-4) && near(r.trace[0].phaseDeg, -45.0, 1e-4), "MA values");
        TEST_CHECK(near(r.trace[1].magnitudeDb, 0.0, 1e-4) && near(r.trace[1].phaseDeg, 10.0, 1e-4), "MA unity");

        // DB in Hz, lower case unit
        std::string db = std::string("# hz s db r 50\n")
            + "100000000 " + junk + " -3.5 120 " + junk + " " + junk + "\n"
            + "100000100 " + junk + " -10 -170 " + junk + " " + junk + "\n";
        r = parseTouchstoneS2P(db);
        TEST_CHECK(r.ok, "DB/Hz must parse: " + r.error);
        TEST_CHECK(near(r.trace[0].freqHz, 1.0e8, 1e-6) && near(r.trace[1].freqHz, 100000100.0, 1e-6), "Hz scaling");
        TEST_CHECK(near(r.trace[0].magnitudeDb, -3.5, 1e-5) && near(r.trace[0].phaseDeg, 120.0, 1e-5), "DB values");
        TEST_CHECK(near(r.trace[1].magnitudeDb, -10.0, 1e-5) && near(r.trace[1].phaseDeg, -170.0, 1e-5), "DB second record");

        // kHz
        r = parseTouchstoneS2P("# kHz S MA R 50\n100000 9 9 0.5 0 9 9 9 9\n100001 9 9 0.5 0 9 9 9 9\n");
        TEST_CHECK(r.ok && near(r.trace[0].freqHz, 1.0e8, 1e-3), "kHz scaling");

        // Wrapped v1 file: each record over three lines, different wrapping per record
        std::string wrapped =
            "! wrapped\n# MHz S RI R 50\n"
            "100 9 9\n  0.3 0.4\n 9 9 9\n 9\n"
            "\n"
            "101 9 9 0.6\n0.8 9 9 9 9\n";
        r = parseTouchstoneS2P(wrapped);
        TEST_CHECK(r.ok, "wrapped file must parse: " + r.error);
        TEST_CHECK(r.trace.size() == 2, "wrapped records");
        TEST_CHECK(near(r.trace[0].magnitudeDb, -6.0206, 1e-3) && near(r.trace[0].phaseDeg, 53.1301, 1e-3), "wrapped record 1");
        TEST_CHECK(near(r.trace[1].magnitudeDb, 0.0, 1e-4) && near(r.trace[1].phaseDeg, 53.1301, 1e-3), "wrapped record 2 (0.6+0.8j)");

        // Errors
        TEST_CHECK(!parseTouchstoneS2P("# MHz S RI R 50\n100 1 2 3\n").ok, "truncated record rejected");
        TEST_CHECK(!parseTouchstoneS2P("# MHz S RI R 50\n100 9 9 1 0 9 9 9 9\n99 9 9 1 0 9 9 9 9\n").ok, "descending frequencies rejected");
        TEST_CHECK(!parseTouchstoneS2P("# MHz Y RI R 50\n100 9 9 1 0 9 9 9 9\n").ok, "non-S parameters rejected");
        TEST_CHECK(!parseTouchstoneS2P("[Version] 2.0\n# MHz S RI R 50\n").ok, "Touchstone 2.0 rejected");
        TEST_CHECK(!parseTouchstoneS2P("# MHz S RI R 50\n100 9 9 x 0 9 9 9 9\n").ok, "bad number rejected");
        std::cout << "  -> PASSED.\n";
    }

    // --------------------------------------------------------------------------------------
    std::cout << "[TEST] 2. File loading: .s1p rejected, extension handling, no fallback data\n";
    {
        std::string s1p = writeFile("dut.s1p", "# MHz S RI R 50\n100 0.5 0.1\n101 0.5 0.1\n");
        auto r = loadTraceFile(s1p);
        TEST_CHECK(!r.ok && r.trace.empty(), ".s1p must be rejected with no data");
        TEST_CHECK(r.error.find("reflection") != std::string::npos, ".s1p message must say reflection: " + r.error);

        std::string s2p = writeFile("dut.S2P", "# MHz S MA R 50\n100 9 9 0.1 -45 9 9 9 9\n101 9 9 0.2 -40 9 9 9 9\n");
        r = loadTraceFile(s2p);
        TEST_CHECK(r.ok && r.trace.size() == 2, "upper-case extension loads: " + r.error);

        r = loadTraceFile((g_dir / "missing.s2p").string());
        TEST_CHECK(!r.ok && r.trace.empty() && !r.error.empty(), "missing file gives an error and no data");
        r = loadTraceFile(writeFile("x.s4p", "# MHz S RI R 50\n"));
        TEST_CHECK(!r.ok, "other port counts rejected");
        r = loadTraceFile(writeFile("x.bin", "1,2\n"));
        TEST_CHECK(!r.ok, "unknown extension rejected");
        r = loadTraceFile("");
        TEST_CHECK(!r.ok, "empty path rejected");
        std::cout << "  -> PASSED.\n";
    }

    // --------------------------------------------------------------------------------------
    std::cout << "[TEST] 3. CSV parsing and export round trip\n";
    {
        auto r = parseTraceCsv("freq_hz,s21_db,phase_deg\n1e8,-3.5,120\n100000100,-10,-170\n");
        TEST_CHECK(r.ok, "headed 3-column CSV: " + r.error);
        TEST_CHECK(r.trace.size() == 2 && near(r.trace[0].freqHz, 1.0e8, 1e-6) && near(r.trace[0].magnitudeDb, -3.5, 1e-5)
                       && near(r.trace[0].phaseDeg, 120.0, 1e-5) && near(r.trace[1].phaseDeg, -170.0, 1e-5),
                   "headed values");

        r = parseTraceCsv("# note\n100000000;-3\n100000100;-4\n100000200;-5\n");
        TEST_CHECK(r.ok && r.trace.size() == 3, "header-less 2-column semicolon CSV: " + r.error);
        TEST_CHECK(near(r.trace[2].magnitudeDb, -5.0, 1e-6) && std::isnan(r.trace[0].phaseDeg), "2-column has no phase (NaN)");

        r = parseTraceCsv("100000000 -3\n100000100 -4\n");
        TEST_CHECK(r.ok, "whitespace separated CSV");

        TEST_CHECK(!parseTraceCsv("freq_mhz,s21_db\n100,-3\n101,-4\n").ok, "MHz header rejected (values are not rescaled)");
        TEST_CHECK(!parseTraceCsv("freq_hz,s21_db\n100000000,-3\n100000100,abc\n").ok, "bad value rejected");
        TEST_CHECK(!parseTraceCsv("freq_hz,s21_db\n100000100,-3\n100000000,-4\n").ok, "descending rejected");
        TEST_CHECK(!parseTraceCsv("freq_hz,s21_db\n100000000,-3\n").ok, "single row rejected");
        TEST_CHECK(!parseTraceCsv("freq_hz,s21_db\n100000000,-3,1,2\n100000100,-3,1,2\n").ok, "four columns rejected");

        // Export then re-import, with and without phase
        std::vector<VNASweepPoint> t;
        for (int i = 0; i < 5; ++i) {
            VNASweepPoint p;
            p.freqHz = 1.0e8 + 1000.0 * i;
            p.magnitudeDb = -3.0f - i;
            p.phaseDeg = (i % 2) ? NAN : 10.0f * i;
            t.push_back(p);
        }
        std::string out = (g_dir / "export.csv").string();
        TEST_CHECK(writeTraceCsv(out, t).empty(), "export succeeds");
        r = loadTraceFile(out);
        TEST_CHECK(r.ok && r.trace.size() == 5, "exported CSV re-imports: " + r.error);
        TEST_CHECK(near(r.trace[3].freqHz, 1.00003e8, 1e-2) && near(r.trace[3].magnitudeDb, -6.0, 1e-5), "exported values");
        TEST_CHECK(near(r.trace[2].phaseDeg, 20.0, 1e-5), "exported phase");
        TEST_CHECK(!writeTraceCsv(out, {}).empty(), "empty trace export refused");
        std::cout << "  -> PASSED.\n";
    }

    // --------------------------------------------------------------------------------------
    std::cout << "[TEST] 4. Scalar sweep: thru, DUT normalization, fit recovers f0\n";
    {
        ScalarSweep sweep;
        FakeProbe probe;
        auto s = notchSettings();
        double spacing = (s.stopHz - s.startHz) / (s.points - 1);

        // DUT before thru is refused and never touches the receiver
        probe.mode = SweepStage::Dut;
        auto r = sweep.run(SweepStage::Dut, s, probe);
        TEST_CHECK(!r.ok && !r.error.empty() && probe.tunes == 0, "DUT without thru refused");

        probe.mode = SweepStage::Thru;
        r = sweep.run(SweepStage::Thru, s, probe);
        TEST_CHECK(r.ok && sweep.hasThru(), "thru ok: " + r.error);
        TEST_CHECK(probe.finishes == 1 && probe.tunes == s.points, "probe finished once, one tune per point");
        TEST_CHECK(near(probe.tuned.front(), s.startHz, 1e-3) && near(probe.tuned.back(), s.stopHz, 1e-3), "grid spans start to stop");
        TEST_CHECK(near(probe.tuned[1] - probe.tuned[0], spacing, 1e-3), "uniform spacing");

        probe.mode = SweepStage::Dut;
        r = sweep.run(SweepStage::Dut, s, probe);
        TEST_CHECK(r.ok && (int)r.trace.size() == s.points, "DUT ok: " + r.error);
        double maxErr = 0.0;
        for (const auto& p : r.trace) {
            maxErr = std::max(maxErr, (double)std::fabs(p.magnitudeDb - probe.notchDb(p.freqHz)));
            TEST_CHECK(std::isnan(p.phaseDeg), "scalar sweep has no phase");
            TEST_CHECK(p.valid && std::isnan(p.dynRangeDb), "without a noise floor nothing is flagged");
        }
        TEST_CHECK(maxErr < 0.05, "normalized S21 must follow the notch (thru ripple cancelled), max error dB = " + std::to_string(maxErr));

        VNAAnalyzer vna;
        vna.setFullTrace(r.trace);
        auto fit = vna.getDipFit();
        TEST_CHECK(fit.valid, "fit on the swept trace");
        TEST_CHECK(near(fit.resonantFreqHz, 100.0e6, spacing), "f0 within one point spacing");
        TEST_CHECK(near(fit.dipDepthDb, 20.0, 1.0), "depth near 20 dB");
        std::cout << "  -> f0 error " << std::fabs(fit.resonantFreqHz - 100.0e6) << " Hz (spacing " << spacing << " Hz), depth " << fit.dipDepthDb << " dB\n";
        std::cout << "  -> PASSED.\n";
    }

    // --------------------------------------------------------------------------------------
    std::cout << "[TEST] 5. Reference invalidation on settings change\n";
    {
        ScalarSweep sweep;
        FakeProbe probe;
        auto s = notchSettings();
        TEST_CHECK(sweep.run(SweepStage::Thru, s, probe).ok, "thru");
        sweep.invalidateIfChanged(s);
        TEST_CHECK(sweep.hasThru(), "unchanged settings keep the reference");

        auto t = s;
        t.settleSec = 0.1; t.dwellSec = 0.05;
        sweep.invalidateIfChanged(t);
        TEST_CHECK(sweep.hasThru(), "settle/dwell do not invalidate");

        struct Case { const char* name; void (*mod)(ScalarSweepSettings&); };
        const Case cases[] = {
            { "start", [](ScalarSweepSettings& x) { x.startHz += 1000.0; } },
            { "stop", [](ScalarSweepSettings& x) { x.stopHz += 1000.0; } },
            { "points", [](ScalarSweepSettings& x) { x.points += 1; } },
            { "rbw", [](ScalarSweepSettings& x) { x.rbwHz *= 2.0; } },
            { "offset", [](ScalarSweepSettings& x) { x.offsetHz += 1000.0; } },
        };
        for (const auto& c : cases) {
            ScalarSweep sw;
            TEST_CHECK(sw.run(SweepStage::Thru, s, probe).ok, "thru");
            probe.mode = SweepStage::Noise;
            TEST_CHECK(sw.run(SweepStage::Noise, s, probe).ok && sw.hasNoise(), "noise");
            probe.mode = SweepStage::Thru;
            auto m = s;
            c.mod(m);
            sw.invalidateIfChanged(m);
            TEST_CHECK(!sw.hasThru() && !sw.hasNoise(), std::string("change of ") + c.name + " invalidates thru and noise");
            probe.mode = SweepStage::Dut;
            int tunesBefore = probe.tunes;
            auto r = sw.run(SweepStage::Dut, m, probe);
            TEST_CHECK(!r.ok && probe.tunes == tunesBefore, std::string("DUT after change of ") + c.name + " refused without measuring");
            probe.mode = SweepStage::Thru;
        }

        // A DUT sweep with settings that differ from the stored thru is refused even without invalidateIfChanged
        auto m = s;
        m.points += 2;
        probe.mode = SweepStage::Dut;
        TEST_CHECK(!sweep.run(SweepStage::Dut, m, probe).ok, "DUT with mismatching grid refused");

        // Timestamp is kept
        TEST_CHECK(sweep.thruTime() != std::chrono::system_clock::time_point{}, "thru timestamp set");
        std::cout << "  -> PASSED.\n";
    }

    // --------------------------------------------------------------------------------------
    std::cout << "[TEST] 6. Noise floor: dynamic range and unreliable points excluded from the fit\n";
    {
        ScalarSweep sweep;
        FakeProbe probe;
        auto s = notchSettings();
        probe.depthDb = 60.0; // bottom of the notch: thru - 60 = about -90 dB, below the -80 dB floor
        TEST_CHECK(sweep.run(SweepStage::Thru, s, probe).ok, "thru");
        probe.mode = SweepStage::Noise;
        TEST_CHECK(sweep.run(SweepStage::Noise, s, probe).ok, "noise");
        probe.mode = SweepStage::Dut;
        auto r = sweep.run(SweepStage::Dut, s, probe);
        TEST_CHECK(r.ok, "DUT");

        size_t flagged = 0;
        for (const auto& p : r.trace) {
            double thru = FakeProbe::thruDb(p.freqHz);
            TEST_CHECK(near(p.dynRangeDb, thru - probe.floorDb, 0.01), "dynamic range = thru - floor");
            double dutMeas = 10.0 * std::log10(std::pow(10.0, (thru + probe.notchDb(p.freqHz)) / 10.0) + std::pow(10.0, probe.floorDb / 10.0));
            bool expectUnreliable = (dutMeas - probe.floorDb) < 3.0;
            TEST_CHECK(p.valid == !expectUnreliable, "valid flag must equal 'DUT >= floor + 3 dB'");
            if (!p.valid) { flagged++; }
        }
        TEST_CHECK(flagged > 0 && flagged < r.trace.size() / 2, "some, but not most, points are flagged");
        TEST_CHECK(!r.trace[r.trace.size() / 2].valid, "notch bottom is unreliable");
        TEST_CHECK(r.trace.front().valid && r.trace.back().valid, "band edges are reliable");

        VNAAnalyzer vna;
        vna.setFullTrace(r.trace);
        auto fit = vna.getDipFit();
        TEST_CHECK(fit.valid, "fit still works on the reliable points");
        bool onValid = false;
        for (const auto& p : r.trace) {
            if (p.freqHz == fit.resonantFreqHz) { onValid = p.valid; }
        }
        TEST_CHECK(onValid, "fit extremum must be a reliable point");

        std::cout << "  -> " << flagged << " of " << r.trace.size() << " points flagged\n";
        std::cout << "  -> PASSED.\n";
    }

    // --------------------------------------------------------------------------------------
    std::cout << "[TEST] 7. Cancellation, probe failure, duration limit\n";
    {
        // Cancel in the middle of a thru sweep
        ScalarSweep sweep;
        FakeProbe probe;
        probe.sweep = &sweep;
        probe.cancelAtMeasure = 10;
        auto s = notchSettings();
        auto r = sweep.run(SweepStage::Thru, s, probe);
        TEST_CHECK(!r.ok && r.cancelled, "sweep reports cancellation");
        TEST_CHECK(!sweep.hasThru(), "cancelled thru is not stored");
        TEST_CHECK(probe.tunes == 10 && probe.finishes == 1, "stops right after the cancelled point and still restores the receiver");

        // run() clears an old cancel flag
        probe.cancelAtMeasure = -1;
        probe.tunes = 0;
        r = sweep.run(SweepStage::Thru, s, probe);
        TEST_CHECK(r.ok && sweep.hasThru() && probe.tunes == s.points, "a new sweep is not affected by the earlier cancel");

        // Tune failure
        FakeProbe bad;
        bad.failTune = true;
        ScalarSweep sw2;
        r = sw2.run(SweepStage::Thru, s, bad);
        TEST_CHECK(!r.ok && !r.cancelled && r.error.find("fake tune failure") != std::string::npos, "tune failure is reported with the probe's text");
        TEST_CHECK(bad.finishes == 1 && !sw2.hasThru(), "receiver restored, nothing stored");

        // Threaded sweep blocked in the probe is released by cancel()
        {
            ScalarSweep async;
            std::atomic<int> finishes{ 0 };
            auto bp = std::make_unique<BlockingProbe>();
            bp->finishes = &finishes;
            BlockingProbe* raw = bp.get();
            TEST_CHECK(async.start(SweepStage::Thru, s, std::move(bp)).empty(), "async start");
            TEST_CHECK(async.isRunning(), "running flag set");
            {
                std::unique_lock<std::mutex> l(raw->m);
                TEST_CHECK(raw->cv.wait_for(l, std::chrono::seconds(5), [raw] { return raw->entered; }), "worker reached the probe");
            }
            TEST_CHECK(!async.start(SweepStage::Thru, s, std::make_unique<FakeProbe>()).empty(), "second start while running refused");
            async.cancel();
            ScalarSweepResult ar;
            bool got = false;
            for (int i = 0; i < 500 && !got; ++i) {
                got = async.pollFinished(ar);
                if (!got) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            }
            TEST_CHECK(got, "cancelled async sweep finishes");
            TEST_CHECK(ar.cancelled && !async.isRunning() && finishes.load() == 1, "async cancel result");
            TEST_CHECK(!async.pollFinished(ar), "result is delivered once");
        }

        // Threaded sweep that completes
        {
            ScalarSweep async;
            auto fp = std::make_unique<FakeProbe>();
            TEST_CHECK(async.start(SweepStage::Thru, s, std::move(fp)).empty(), "async start");
            ScalarSweepResult ar;
            bool got = false;
            for (int i = 0; i < 500 && !got; ++i) {
                got = async.pollFinished(ar);
                if (!got) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            }
            TEST_CHECK(got && ar.ok && async.hasThru() && near(async.progress(), 1.0, 1e-6), "async sweep completes with full progress");
        }

        // Duration limit: points * (settle + dwell) above 10 minutes is refused
        ScalarSweepSettings big = s;
        big.points = 4001;
        big.settleSec = 0.2;
        big.dwellSec = 0.1;
        TEST_CHECK(near(ScalarSweep::estimateSeconds(big), 1200.3, 1e-6), "estimate = points * (settle + dwell)");
        TEST_CHECK(!ScalarSweep::validate(big).empty(), "20 minute sweep refused");
        FakeProbe untouched;
        ScalarSweep sw3;
        TEST_CHECK(!sw3.run(SweepStage::Thru, big, untouched).ok && untouched.tunes == 0, "refused sweep does not touch the receiver");
        TEST_CHECK(ScalarSweep::validate(s).empty(), "default sweep is accepted");
        auto badRange = s;
        badRange.stopHz = badRange.startHz;
        TEST_CHECK(!ScalarSweep::validate(badRange).empty(), "empty span refused");
        auto zeroOffset = s;
        zeroOffset.offsetHz = 0.0;
        TEST_CHECK(!ScalarSweep::validate(zeroOffset).empty(), "zero offset (DC spike in the VFO) refused");
        std::cout << "  -> PASSED.\n";
    }

    // --------------------------------------------------------------------------------------
    std::cout << "[TEST] 8. Analyzer starts empty, NaN phase and invalid points\n";
    {
        VNAAnalyzer vna;
        TEST_CHECK(vna.getTrace().empty() && !vna.getDipFit().valid, "no data until something is loaded");
        vna.setTraceSource("test");
        TEST_CHECK(vna.getTraceSource() == "test", "trace source kept");
        vna.clearTrace();
        TEST_CHECK(vna.getTraceSource().empty(), "clear resets the source");

        // A trace of NaN phases fits fine; one invalid point at the bottom does not become the extremum
        std::vector<VNASweepPoint> t;
        for (int i = 0; i < 41; ++i) {
            VNASweepPoint p;
            p.freqHz = 1.0e6 * i;
            double d = (i - 20) / 3.0;
            p.magnitudeDb = (float)(-20.0 + 20.0 * (1.0 - 1.0 / (1.0 + d * d)));
            p.phaseDeg = NAN;
            t.push_back(p);
        }
        t[20].valid = false;
        vna.setFullTrace(t);
        auto fit = vna.getDipFit();
        TEST_CHECK(fit.valid && fit.resonantFreqHz != 20.0e6, "fit skips the flagged point");
        std::cout << "  -> PASSED.\n";
    }

    fs::remove_all(g_dir);
    std::cout << "============================================================\n";
    std::cout << "ALL RESONATOR TESTS PASSED (fake receiver only, no real source)\n";
    return 0;
}
