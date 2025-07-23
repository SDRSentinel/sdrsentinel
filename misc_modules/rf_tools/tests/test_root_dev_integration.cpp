#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <complex>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <chrono>
#include <random>
#include <algorithm>

#include "pdw/power_cal.h"
#include "export/pdw_binary_exporter.h"
#include "mop/chirp_matched_filter.h"
#include "vna/vna_analyzer.h"
#include <utils/flog.h>

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\n[TEST FAILED] " << msg << " (" #cond " at " << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

// Test-only Lorentzian notch generator (the module itself has no synthetic data):
// S21(f) = 1 - (1 - S21_min) / (1 + j * 2 * Q * (f - f0) / f0), 201 points over 8 bandwidths
static std::vector<rf_tools::vna::VNASweepPoint> makeLorentzianNotch(double f0, double q, float depthDb) {
    std::vector<rf_tools::vna::VNASweepPoint> trace;
    double bw = f0 / q;
    double span = bw * 8.0;
    double start = f0 - span * 0.5;
    const size_t n = 201;
    double df = span / (n - 1);
    float s21MinLin = std::pow(10.0f, -depthDb / 20.0f);
    for (size_t i = 0; i < n; ++i) {
        double f = start + i * df;
        double deltaF = (f - f0) / (0.5 * bw);
        std::complex<double> s21 = 1.0 - (1.0 - s21MinLin) / std::complex<double>(1.0, deltaF);
        rf_tools::vna::VNASweepPoint pt;
        pt.freqHz = f;
        pt.magnitudeDb = static_cast<float>(20.0 * std::log10(std::abs(s21)));
        pt.phaseDeg = static_cast<float>(std::arg(s21) * 180.0 / M_PI);
        trace.push_back(pt);
    }
    return trace;
}

int main() {
    flog::logLevel = flog::TYPE_WARNING;
    std::cout << "============================================================\n";
    std::cout << "   rf_tools Software Self-Consistency Test Suite            \n";
    std::cout << "============================================================\n";

    // ------------------------------------------------------------------------
    // TEST 1: Power Calibration Engine (GTRI reference values) (dBFS -> dBm & B210 Range Check)
    // ------------------------------------------------------------------------
    std::cout << "[TEST] 1. Power calibration (checked against GTRI gr-pdw reference values; B210 range check)...\n";
    {
        rf_tools::PowerCalibrationTable calTable;
        calTable.loadDefaultB210();
        calTable.setEnabled(true);

        // Verify official AD9361 hardware range [70 MHz - 6.0 GHz]
        TEST_CHECK(calTable.isFrequencyInOfficialHardwareRange(1000.0e6), "1 GHz must be within official B210 range");
        TEST_CHECK(!calTable.isFrequencyInOfficialHardwareRange(50.0e6), "50 MHz must be flagged outside official AD9361 spec [70 MHz - 6 GHz]");

        TEST_CHECK(calTable.isLoaded(), "Embedded GTRI B210 table failed to load");

        // Reference values computed from GTRI's b210_pdw.pickle (gr-pdw) with a restricted unpickler:
        // bilinear interpolation (frequency, then gain) between the measured points, NaN points skipped.
        struct RefPoint { int ch; const char* port; double freq; float gain; float dbm; };
        const RefPoint refs[] = {
            { 0, "RX2",   1.0e9,  40.0f, -27.0226f }, // 1 GHz / 40 dB quoted in the roadmap
            { 0, "RX2",   1.0e9,  50.0f, -29.6226f },
            { 0, "RX2",   1.0e9,  60.0f, -50.4472f }, // 20.8 dB drop between 50 and 60 dB gain: non-linear
            { 0, "RX2",   1.0e9,  70.0f, -60.2406f },
            { 0, "RX2",   1.0e9,   0.0f,   6.7410f },
            { 0, "RX2", 100.0e6,  20.0f, -11.5516f },
            { 0, "RX2",   2.4e9,  30.0f, -15.2458f },
            { 0, "RX2",   5.8e9,  10.0f,  12.1571f },
            { 0, "TX/RX", 1.0e9,  40.0f, -27.0296f },
            { 1, "RX2",   1.0e9,  40.0f, -24.2511f }, // channel 1 differs from channel 0
        };
        for (const auto& r : refs) {
            float v = calTable.getRefLevelDbm(r.freq, r.gain, r.port, r.ch);
            std::cout << "  -> GTRI ref ch" << r.ch << " " << r.port << " " << r.freq / 1e6 << " MHz, " << r.gain
                      << " dB: expected " << r.dbm << " dBm, got " << v << " dBm\n";
            TEST_CHECK(std::abs(v - r.dbm) < 0.05f, "Reference level differs from GTRI table");
        }

        // The table has data for channel 1 that differs from channel 0, so the selected channel must change calibrate()
        std::cout << "  -> Sweeping 50 MHz..6 GHz x 0..80 dB for NaN (both channels, both ports)...\n";
        bool anyNan = false;
        for (int ch = 0; ch < 2; ++ch) {
            for (const char* port : { "RX2", "TX/RX" }) {
                for (double f = 50.0e6; f <= 6.05e9; f += 25.0e6) {
                    for (float g = 0.0f; g <= 80.0f; g += 2.5f) {
                        if (std::isnan(calTable.getRefLevelDbm(f, g, port, ch))) anyNan = true;
                    }
                }
            }
        }
        TEST_CHECK(!anyNan, "Reference level interpolation returned NaN");

        // Reference level is plausible (for B210 at 40 dB gain, ref level is around -20 to -30 dBm)
        double testFreq = 1000.0e6; // 1 GHz
        float testGain = 40.0f;     // 40 dB gain
        float refLevel = calTable.getRefLevelDbm(testFreq, testGain, "RX2");
        TEST_CHECK(refLevel < 0.0f && refLevel > -50.0f, "Reference level out of expected range");

        rf_tools::PDW pdw;
        pdw.absoluteFrequency = testFreq;
        pdw.peakPower = -10.0f; // -10 dBFS
        pdw.averagePower = -15.0f;
        pdw.noiseFloor = -60.0f;

        calTable.setHardwareGain(testGain);
        calTable.calibrate(pdw);

        TEST_CHECK(pdw.isCalibratedDbm, "PDW must be marked calibrated");
        float expectedPk = -10.0f + refLevel;
        TEST_CHECK(std::abs(pdw.peakPowerDbm - expectedPk) < 1e-4f, "Calibrated peak power mismatch");

        calTable.setChannel(1);
        rf_tools::PDW pdw1 = pdw;
        calTable.calibrate(pdw1);
        std::cout << "  -> channel setter: ch0 ref " << pdw.refLevelDbm << " dBm, ch1 ref " << pdw1.refLevelDbm << " dBm\n";
        TEST_CHECK(std::abs(pdw1.refLevelDbm - (-24.2511f)) < 0.05f, "calibrate() must use the selected channel (ch1 GTRI reference)");
        TEST_CHECK(std::abs(pdw1.refLevelDbm - pdw.refLevelDbm) > 1.0f, "Channel 1 must differ from channel 0");
        calTable.setChannel(0);

        // Out-of-spec test (50 MHz legacy GTRI calibration curve)
        float refLevel50M = calTable.getRefLevelDbm(50.0e6, testGain, "RX2");
        TEST_CHECK(refLevel50M < 0.0f, "50 MHz empirical ref level valid");

        std::cout << "  -> At 1 GHz, Gain 40 dB: 0 dBFS = " << refLevel << " dBm\n";
        std::cout << "  -> -10 dBFS calibrated to: " << pdw.peakPowerDbm << " dBm\n";
        std::cout << "  -> Official AD9361 range check [70 MHz - 6 GHz] verified.\n";
        std::cout << "  -> PASSED.\n";
    }

    // ------------------------------------------------------------------------
    // TEST 2: Binary PDW Exporter (self-consistency)
    // ------------------------------------------------------------------------
    std::cout << "[TEST] 2. Binary PDW serialization (header and record sizes, sample rate, time base)...\n";
    {
        rf_tools::PdwBinaryExporter exporter;
        std::string testPath = "test_output/pdw_test.pdwbin";
        std::filesystem::create_directories("test_output");

        const double fs = 2.4e6;
        const double timeBase = 1700000000.25;
        bool ok = exporter.startRecording(testPath, fs, 433.92e6, -25.0f, true, timeBase);
        TEST_CHECK(ok, "Failed to start PDW binary recording");

        for (uint64_t i = 1; i <= 10; ++i) {
            rf_tools::PDW p;
            p.pulseIndex = i;
            p.sampleIndex = i * 2000;
            p.toa = static_cast<double>(p.sampleIndex) / fs;
            p.courseToa = static_cast<int64_t>(std::floor(p.toa));
            p.fineToa = static_cast<uint64_t>(std::round((p.toa - std::floor(p.toa)) * fs));
            p.pulseWidth = 10.0e-6;
            p.pulseWidthSamples = 20;
            p.peakPower = -15.0f;
            p.averagePower = -18.0f;
            p.noiseFloor = -70.0f;
            p.snr = 52.0f;
            p.isCalibratedDbm = true;
            p.peakPowerDbm = -40.0f;
            p.averagePowerDbm = -43.0f;
            p.noiseFloorDbm = -95.0f;
            p.pri = 0.001;
            exporter.writePulse(p);
        }

        TEST_CHECK(exporter.getRecordCount() == 10, "Exporter pulse count mismatch");
        exporter.stopRecording();
        TEST_CHECK(!exporter.isRecording(), "Exporter should be stopped");

        auto fsize = std::filesystem::file_size(testPath);
        size_t expectedSize = sizeof(rf_tools::PdwBinaryHeader) + 10 * sizeof(rf_tools::PdwBinaryRecord);
        TEST_CHECK(fsize == expectedSize, "File size does not match exact sizeof(Header) + 10 * sizeof(Record)");

        std::ifstream hf(testPath, std::ios::binary);
        rf_tools::PdwBinaryHeader hdr;
        hf.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
        TEST_CHECK(hf.good(), "Could not read PDW header back");
        TEST_CHECK(std::memcmp(hdr.magic, "SDRPPPDW", 8) == 0, "Header magic mismatch");
        TEST_CHECK(hdr.version == 2, "Header version mismatch");
        TEST_CHECK(hdr.headerSize == sizeof(rf_tools::PdwBinaryHeader), "Header size field mismatch");
        TEST_CHECK(hdr.recordSize == sizeof(rf_tools::PdwBinaryRecord), "Record size field mismatch");
        TEST_CHECK(hdr.sampleRate == fs, "Header sample rate was not the one passed in");
        TEST_CHECK(hdr.timeBaseUnix == timeBase, "Header time base mismatch");

        std::cout << "  -> Recorded 10 PDWs, File size: " << fsize << " bytes\n";
        std::cout << "  -> PASSED.\n";
    }

    // ------------------------------------------------------------------------
    // TEST 3: Chirp matched filter: AWGN noise, windowing, Doppler (self-consistency)
    // ------------------------------------------------------------------------
    std::cout << "[TEST] 3. Chirp matched filter on synthetic LFM (ideal, AWGN, Doppler, windowing)...\n";
    {
        rf_tools::mop::ChirpMatchedFilter mf;
        double fs = 10.0e6; // 10 MSPS
        mf.init(fs);

        double duration = 20.0e-6; // 20 us pulse
        double bandwidth = 4.0e6;  // 4 MHz chirp sweep
        double chirpRate = bandwidth / duration; // 2.0e11 Hz/s
        double theoreticalGainDb = 10.0 * std::log10(bandwidth * duration); // 10*log10(80) = 19.03 dB

        // 4A: Ideal noiseless LFM compression
        auto cleanChirp = mf.generateReferenceChirp(duration, -bandwidth * 0.5, chirpRate);
        TEST_CHECK(cleanChirp.size() == 200, "Clean chirp sample count mismatch");

        auto compClean = mf.compressPulse(cleanChirp, chirpRate);
        TEST_CHECK(compClean.detected, "Clean chirp must be detected");
        TEST_CHECK(compClean.compressionRatio > 40.0f, "Compression ratio should be ~50x");
        TEST_CHECK(compClean.pslrDb < -12.5f, "Sinc unwindowed PSLR should be ~-13.2 dB");
        std::cout << "  [4A: Ideal] Compression Ratio: " << compClean.compressionRatio << "x | PSLR: " << compClean.pslrDb << " dB\n";

        // 4B: Windowed LFM compression (Hamming window for sidelobe suppression)
        auto compWindowed = mf.compressPulse(cleanChirp, chirpRate, rf_tools::mop::ChirpMatchedFilter::WindowType::HAMMING);
        TEST_CHECK(compWindowed.detected, "Windowed chirp must be detected");
        TEST_CHECK(compWindowed.pslrDb < -28.0f, "Hamming windowed PSLR must be < -28 dB");
        std::cout << "  [4B: Windowed] Hamming PSLR: " << compWindowed.pslrDb << " dB (Sidelobe suppression > 15 dB)\n";

        // 4C: AWGN Noisy Signal Processing Gain Test
        // Test at 0 dB input SNR and -6 dB input SNR
        std::mt19937 rng(42);
        std::normal_distribution<float> dist(0.0f, 1.0f);

        for (float inputSnrDb : { 0.0f, -6.0f }) {
            float noisePower = 1.0f / std::pow(10.0f, inputSnrDb / 10.0f);
            float sigma = std::sqrt(noisePower * 0.5f); // per I/Q branch

            std::vector<std::complex<float>> noisyChirp = cleanChirp;
            for (auto& s : noisyChirp) {
                s += std::complex<float>(dist(rng) * sigma, dist(rng) * sigma);
            }

            auto compNoisy = mf.compressPulse(noisyChirp, chirpRate);
            TEST_CHECK(compNoisy.detected, "Noisy chirp must be detected");

            // Measure empirical noise floor in output envelope outside mainlobe
            // Linear convolution of two length-N vectors has overlap L(i) = min(i+1, 2N-1-i).
            // Compensate for this triangular window to obtain unbiased noise floor.
            size_t envLen = compNoisy.compressedEnvelope.size();
            double noiseSumSq = 0.0;
            size_t noiseSamples = 0;
            for (size_t i = 0; i < envLen; ++i) {
                if (i < 140 || i > 260) {
                    double overlap = std::min(i + 1, 2 * cleanChirp.size() - 1 - i);
                    if (overlap > 20.0) {
                        double normalizedNoise = compNoisy.compressedEnvelope[i] / std::sqrt(overlap / static_cast<double>(cleanChirp.size()));
                        noiseSumSq += normalizedNoise * normalizedNoise;
                        noiseSamples++;
                    }
                }
            }
            double noiseVar = noiseSumSq / noiseSamples;
            double outputSnrDb = -10.0 * std::log10(noiseVar + 1e-12);
            double measuredFullGainDb = outputSnrDb - inputSnrDb; // matches 10*log10(N) = 23.01 dB
            double inBandGainDb = measuredFullGainDb - 10.0 * std::log10(fs / bandwidth); // matches 10*log10(BT) = 19.03 dB

            std::cout << "  [4C: AWGN] Input SNR: " << inputSnrDb << " dB -> Output SNR: " << outputSnrDb
                      << " dB | In-Band Gain: +" << inBandGainDb << " dB (Theory 10*log10(BT): " << theoreticalGainDb << " dB)"
                      << " | Full Nyquist Gain: +" << measuredFullGainDb << " dB (Theory 10*log10(N): 23.01 dB)\n";

            // Verify empirical processing gain matches 10*log10(BT) within statistical noise margin (+/- 1.5 dB)
            TEST_CHECK(std::abs(inBandGainDb - theoreticalGainDb) < 1.5, "In-band processing gain diverges from 10*log10(BT)");
        }

        // 4D: Doppler frequency offset (+50 kHz shift)
        double dopplerShiftHz = 50.0e6 * 1e-3; // 50 kHz
        std::vector<std::complex<float>> dopplerChirp = cleanChirp;
        for (size_t n = 0; n < dopplerChirp.size(); ++n) {
            double t = n / fs;
            double phase = 2.0 * M_PI * dopplerShiftHz * t;
            dopplerChirp[n] *= std::complex<float>(std::cos(phase), std::sin(phase));
        }
        auto compDoppler = mf.compressPulse(dopplerChirp, chirpRate);
        TEST_CHECK(compDoppler.detected, "Doppler shifted chirp must still be detected");
        // The envelope is normalized to its own peak, so absolute peak loss is not measured; the checks bound the
        // compression ratio and the PSLR change relative to the zero-Doppler case.
        TEST_CHECK(compDoppler.compressionRatio > 40.0f, "Compression ratio with +50 kHz Doppler should stay above 40x");
        TEST_CHECK(compDoppler.pslrDb < compClean.pslrDb + 3.0f, "+50 kHz Doppler must degrade PSLR by less than 3 dB");
        std::cout << "  [4D: Doppler] +50 kHz: compression ratio " << compDoppler.compressionRatio << "x (zero Doppler " << compClean.compressionRatio
                  << "x), PSLR " << compDoppler.pslrDb << " dB (zero Doppler " << compClean.pslrDb << " dB)\n";
        std::cout << "  -> PASSED.\n";
    }

    // ------------------------------------------------------------------------
    // TEST 4: Resonator S21 magnitude analysis on a test-generated notch (self-consistency)
    // ------------------------------------------------------------------------
    std::cout << "[TEST] 4. Resonator S21 magnitude analysis (Q_L, Q_i, Q_c on a synthetic Lorentzian notch)...\n";
    {
        rf_tools::vna::VNAAnalyzer vna;
        double trueF0 = 100.0e6;
        double trueQ = 2000.0;
        float trueDepthDb = 20.0f;

        // In a canonical notch resonator with depth = 20 dB:
        // S21_min = 10^(-20/20) = 0.1
        // Q_L = 2000
        // Q_i = Q_L / S21_min = 2000 / 0.1 = 20000
        // Q_c = Q_L / (1 - S21_min) = 2000 / 0.9 = 2222.2
        // Depth = 20 * log10(Q_i / Q_L) = 20 * log10(10) = 20.0 dB
        double expectedQi = trueQ / std::pow(10.0, -trueDepthDb / 20.0);
        double expectedQc = trueQ / (1.0 - std::pow(10.0, -trueDepthDb / 20.0));

        vna.setFullTrace(makeLorentzianNotch(trueF0, trueQ, trueDepthDb));
        auto fit = vna.getDipFit();

        TEST_CHECK(fit.valid, "Resonator fit must be valid");
        double fErr = std::abs(fit.resonantFreqHz - trueF0);
        double qErrRel = std::abs(fit.loadedQ - trueQ) / trueQ;
        double qiErrRel = std::abs(fit.internalQ - expectedQi) / expectedQi;
        double qcErrRel = std::abs(fit.couplingQ - expectedQc) / expectedQc;
        double depthErr = std::abs(fit.dipDepthDb - trueDepthDb);

        // Compare against the parameters used to generate the synthetic sweep
        TEST_CHECK(fErr < 1000.0, "Resonant frequency error must be < 1 kHz");
        TEST_CHECK(qErrRel < 0.02, "Loaded Q error must be < 2%");
        TEST_CHECK(qiErrRel < 0.02, "Internal Q error must be < 2%");
        TEST_CHECK(qcErrRel < 0.02, "Coupling Q error must be < 2%");
        TEST_CHECK(depthErr < 0.2f, "Resonance depth error must be < 0.2 dB");

        // Mathematical identity checks:
        // 1/Q_L = 1/Q_i + 1/Q_c
        double invQL = 1.0 / fit.loadedQ;
        double invSum = (1.0 / fit.internalQ) + (1.0 / fit.couplingQ);
        TEST_CHECK(std::abs(invQL - invSum) < 1e-7, "1/Q_L must equal 1/Q_i + 1/Q_c");

        // 20*log10(Q_i / Q_L) == dipDepthDb
        double derivedDepthDb = 20.0 * std::log10(fit.internalQ / fit.loadedQ);
        TEST_CHECK(std::abs(derivedDepthDb - fit.dipDepthDb) < 0.1, "20*log10(Q_i/Q_L) must match dip depth");

        std::cout << "  -> True f0: " << trueF0 / 1e6 << " MHz | Fitted f0: " << fit.resonantFreqHz / 1e6 << " MHz\n";
        std::cout << "  -> True Q_L: " << trueQ << " | Fitted Loaded Q: " << fit.loadedQ << " (Error: " << qErrRel * 100.0 << "%)\n";
        std::cout << "  -> Expected Q_i: " << expectedQi << " | Fitted Internal Q: " << fit.internalQ << " (Error: " << qiErrRel * 100.0 << "%)\n";
        std::cout << "  -> Expected Q_c: " << expectedQc << " | Fitted Coupling Q: " << fit.couplingQ << " (Error: " << qcErrRel * 100.0 << "%)\n";
        std::cout << "  -> Fitted Dip Depth: " << fit.dipDepthDb << " dB (matches 20*log10(Q_i/Q_L) = " << derivedDepthDb << " dB)\n";
        std::cout << "  -> PASSED.\n";
    }

    // ------------------------------------------------------------------------
    // TEST 5: Real-time CPU DSP Benchmarks & Execution Characteristics
    // ------------------------------------------------------------------------
    std::cout << "[TEST] 5. Real-time CPU Throughput Benchmarking...\n";
    {
        // Chirp Matched Filter Throughput
        rf_tools::mop::ChirpMatchedFilter mf;
        mf.init(10.0e6);
        auto chirp = mf.generateReferenceChirp(20.0e-6, -2.0e6, 2.0e11);

        size_t numPulses = 5000;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < numPulses; ++i) {
            auto res = mf.compressPulse(chirp, 2.0e11);
            (void)res;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsedSec = std::chrono::duration<double>(t1 - t0).count();
        double pulsesPerSec = numPulses / elapsedSec;
        double equivalentMsps = (numPulses * chirp.size() / 1e6) / elapsedSec;

        std::cout << "  [Benchmark 5: Chirp MF] " << pulsesPerSec << " pulses/sec (" << equivalentMsps << " MSPS sustained FFTW CPU rate)\n";
        TEST_CHECK(pulsesPerSec > 5000.0, "Matched filter must process > 5,000 pulses/sec on modern CPU");

        std::cout << "  -> PASSED.\n";
    }

    std::cout << "\n============================================================\n";
    std::cout << "   ALL TESTS PASSED                                         \n";
    std::cout << "============================================================\n";
    std::cout << "   What was checked:                                        \n";
    std::cout << "   - Test 1: power calibration vs GTRI reference values     \n";
    std::cout << "   - Tests 2-5: software self-consistency on synthetic data \n";
    std::cout << "   Real hardware (USRP B210, TwinRX) was NOT tested.        \n";
    std::cout << "============================================================\n";
    return 0;
}
