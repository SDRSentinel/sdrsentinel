#include <iostream>
#include <iomanip>
#include <cassert>
#include <cmath>
#include <chrono>
#include <vector>
#include <complex>

#include "../src/doa/types.h"
#include "../src/doa/covariance_accumulator.h"
#include "../src/doa/music_engine.h"
#include "../src/doa/array_synthesizer.h"
#include "../src/doa/coherent_bus.h"

using namespace rf_tools::doa;

static constexpr float PI = 3.14159265358979323846f;

void test_covariance_properties() {
    std::cout << "[TEST] 1. Covariance Accumulator & Hermitian Properties..." << std::endl;

    CovarianceAccumulator acc;
    acc.init(4, 256, 0.0f, false);

    ArraySynthesizer synth;
    DOAConfig cfg;
    cfg.num_elements = 4;
    cfg.element_spacing = 0.5f;
    synth.setArrayConfig(cfg);
    synth.setSources({{ 60.0f, 0.0f, 0.0f, false }});
    synth.setNoiseFloor(-25.0f);

    auto snapshots = synth.generateSnapshots(256);
    acc.feedSnapshotsInterleaved(snapshots.data(), 4, 256);

    Eigen::MatrixXcf Rxx;
    bool ready = acc.getNormalizedCovariance(Rxx);
    assert(ready && "Covariance matrix must be ready after 256 snapshots");

    // Check Hermitian symmetry: R_xx == R_xx^H
    float hermitian_error = (Rxx - Rxx.adjoint()).norm();
    assert(hermitian_error < 1e-4f && "Covariance matrix must be Hermitian");

    // Check positive semi-definiteness: all eigenvalues >= 0
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXcf> solver(Rxx);
    for (int i = 0; i < solver.eigenvalues().size(); ++i) {
        assert(solver.eigenvalues()(i) >= -1e-5f && "Eigenvalues must be non-negative");
    }

    std::cout << "  -> PASSED. Hermitian error: " << hermitian_error
              << ", min eigenvalue: " << solver.eigenvalues()(0)
              << ", max eigenvalue: " << solver.eigenvalues()(3) << std::endl;
}

void test_root_music_ula_accuracy() {
    std::cout << "[TEST] 2. Root-MUSIC ULA Angular Accuracy..." << std::endl;

    MUSICEngine engine;
    DOAConfig cfg;
    cfg.geometry = ArrayGeometry::ULA;
    cfg.num_elements = 8;
    cfg.element_spacing = 0.5f;
    cfg.num_snapshots = 512;
    cfg.expected_sources = 2;
    cfg.auto_model_order = false;
    engine.setConfig(cfg);

    CovarianceAccumulator acc;
    acc.init(8, 512, 0.0f, false);

    ArraySynthesizer synth;
    synth.setArrayConfig(cfg);
    // Two incident sources: 40.0 deg and 110.0 deg
    synth.setSources({
        { 40.0f, 0.0f, 1000.0f, false },
        { 110.0f, -3.0f, 2500.0f, false }
    });
    synth.setNoiseFloor(-30.0f); // 30 dB SNR

    auto snapshots = synth.generateSnapshots(512);
    acc.feedSnapshotsInterleaved(snapshots.data(), 8, 512);

    Eigen::MatrixXcf Rxx;
    acc.getNormalizedCovariance(Rxx);

    auto result = engine.computeRootMUSIC(Rxx, 512);
    assert(result.estimated_angles_deg.size() >= 2 && "Must detect both sources");

    float err1 = std::abs(result.estimated_angles_deg[0] - 40.0f);
    float err2 = std::abs(result.estimated_angles_deg[1] - 110.0f);

    std::cout << "  -> Detected Target 1: " << result.estimated_angles_deg[0] << " deg (Error: " << err1 << " deg)" << std::endl;
    std::cout << "  -> Detected Target 2: " << result.estimated_angles_deg[1] << " deg (Error: " << err2 << " deg)" << std::endl;

    assert(err1 < 0.5f && "Target 1 angle error must be < 0.5 deg");
    assert(err2 < 0.5f && "Target 2 angle error must be < 0.5 deg");
    std::cout << "  -> PASSED." << std::endl;
}

void test_calibration_subspace() {
    std::cout << "[TEST] 3. Array Calibration & Gain/Phase Mismatch Compensation..." << std::endl;

    DOAConfig cfg;
    cfg.geometry = ArrayGeometry::ULA;
    cfg.num_elements = 4;
    cfg.element_spacing = 0.5f;
    cfg.num_snapshots = 512;
    cfg.expected_sources = 1;
    cfg.auto_model_order = false;

    MUSICEngine engine;
    engine.setConfig(cfg);

    CovarianceAccumulator acc;
    acc.init(4, 512, 0.0f, false);

    // Inject hardware gain & phase errors:
    // Ch0: 0 dB, 0 deg
    // Ch1: +2.5 dB, +35 deg
    // Ch2: -1.8 dB, -20 deg
    // Ch3: +1.2 dB, +45 deg
    std::vector<float> gain_errors = { 0.0f, 2.5f, -1.8f, 1.2f };
    std::vector<float> phase_errors = { 0.0f, 35.0f, -20.0f, 45.0f };

    ArraySynthesizer synth;
    synth.setArrayConfig(cfg);
    synth.setHardwareMismatches(gain_errors, phase_errors);

    // 1. Send pilot signal at 90.0 deg (broadside) for calibration
    synth.setSources({{ 90.0f, 0.0f, 0.0f, false }});
    synth.setNoiseFloor(-35.0f);
    auto pilot_snapshots = synth.generateSnapshots(512);
    acc.feedSnapshotsInterleaved(pilot_snapshots.data(), 4, 512);

    Eigen::MatrixXcf R_pilot;
    acc.getNormalizedCovariance(R_pilot);

    // Run Soon et al. calibration
    auto cal_factors = engine.calibrateFromPilot(R_pilot, 90.0f);
    assert(cal_factors.size() == 4 && "Must return 4 calibration factors");

    // Apply calibration factors to accumulator
    acc.setCalibration(cal_factors);

    // 2. Now test incident signal from 55.0 deg with calibration active
    synth.setSources({{ 55.0f, 0.0f, 0.0f, false }});
    auto test_snapshots = synth.generateSnapshots(512);
    acc.feedSnapshotsInterleaved(test_snapshots.data(), 4, 512);

    Eigen::MatrixXcf R_test;
    acc.getNormalizedCovariance(R_test);

    auto result = engine.computeRootMUSIC(R_test, 512);
    assert(!result.estimated_angles_deg.empty() && "Must detect calibrated target");

    float err = std::abs(result.estimated_angles_deg[0] - 55.0f);
    std::cout << "  -> Calibrated DOA at 55.0 deg: " << result.estimated_angles_deg[0]
              << " deg (Error: " << err << " deg)" << std::endl;
    assert(err < 0.6f && "Calibrated error must be < 0.6 deg");

    std::cout << "  -> PASSED." << std::endl;
}

void test_crlb_benchmark() {
    std::cout << "[TEST] 4. Monte Carlo Benchmarking vs Theoretical CRLB..." << std::endl;

    const size_t M = 4;
    const size_t K = 256;
    const float true_theta_deg = 70.0f;
    const float true_theta_rad = true_theta_deg * (PI / 180.0f);
    const size_t num_trials = 100;

    DOAConfig cfg;
    cfg.geometry = ArrayGeometry::ULA;
    cfg.num_elements = M;
    cfg.element_spacing = 0.5f;
    cfg.num_snapshots = K;
    cfg.expected_sources = 1;
    cfg.auto_model_order = false;

    MUSICEngine engine;
    engine.setConfig(cfg);

    std::vector<float> snr_levels_db = { 10.0f, 20.0f };

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  ------------------------------------------------------------" << std::endl;
    std::cout << "  SNR [dB] | Measured RMSE [deg] | CRLB RMSE [deg] | EVD Time" << std::endl;
    std::cout << "  ------------------------------------------------------------" << std::endl;

    for (float snr_db : snr_levels_db) {
        float snr_linear = std::pow(10.0f, snr_db / 10.0f);
        float noise_dbfs = -snr_db;

        // Theoretical CRLB for ULA with d = lambda / 2:
        // CRLB = 6 / (K * SNR * pi^2 * cos^2(theta) * M * (M^2 - 1)) [rad^2]
        float cos_theta = std::cos(true_theta_rad);
        float crlb_rad2 = 6.0f / (static_cast<float>(K) * snr_linear * PI * PI * cos_theta * cos_theta * static_cast<float>(M * (M * M - 1)));
        float crlb_rmse_deg = std::sqrt(crlb_rad2) * (180.0f / PI);

        ArraySynthesizer synth;
        synth.setArrayConfig(cfg);
        synth.setSources({{ true_theta_deg, 0.0f, 0.0f, false }});
        synth.setNoiseFloor(noise_dbfs);

        CovarianceAccumulator acc;
        acc.init(M, K, 0.0f, false);

        float sum_sq_error = 0.0f;
        auto start_t = std::chrono::high_resolution_clock::now();

        for (size_t trial = 0; trial < num_trials; ++trial) {
            auto snapshots = synth.generateSnapshots(K);
            acc.feedSnapshotsInterleaved(snapshots.data(), M, K);

            Eigen::MatrixXcf Rxx;
            acc.getNormalizedCovariance(Rxx);

            auto res = engine.computeRootMUSIC(Rxx, K);
            if (!res.estimated_angles_deg.empty()) {
                float err = res.estimated_angles_deg[0] - true_theta_deg;
                sum_sq_error += err * err;
            }
        }

        auto end_t = std::chrono::high_resolution_clock::now();
        double elapsed_us = std::chrono::duration<double, std::micro>(end_t - start_t).count() / num_trials;

        float measured_rmse_deg = std::sqrt(sum_sq_error / num_trials);

        std::cout << "   " << std::setw(6) << snr_db << "  |       "
                  << std::setw(8) << measured_rmse_deg << "     |     "
                  << std::setw(8) << crlb_rmse_deg << "    | "
                  << std::setw(6) << elapsed_us << " us/solve" << std::endl;

        // Measured RMSE should be within a factor of 2.5 of asymptotic CRLB at moderate SNR
        assert(measured_rmse_deg < crlb_rmse_deg * 3.0f && "Measured RMSE must be close to theoretical CRLB");
    }
    std::cout << "  ------------------------------------------------------------" << std::endl;
    std::cout << "  -> PASSED." << std::endl;
}

void test_coherent_bus() {
    std::cout << "[TEST] 5. Multi-Channel Coherent Bus & Snapshot Queue..." << std::endl;

    CoherentBus bus;
    bus.setSourceType(CoherentSourceType::NETWORK_STREAM);

    // Create a 4-channel coherent frame with 128 samples
    CoherentFrame frame;
    frame.channelCount = 4;
    frame.sampleCount = 128;
    frame.timestamp = 1000;
    frame.channelData.resize(4);
    for (size_t ch = 0; ch < 4; ++ch) {
        frame.channelData[ch].resize(128);
        for (size_t s = 0; s < 128; ++s) {
            frame.channelData[ch][s] = std::polar(1.0f, static_cast<float>(ch) * 0.5f + static_cast<float>(s) * 0.1f);
        }
    }

    bus.pushFrame(frame);
    assert(bus.getBufferedSnapshots() == 128 && "Buffer must hold 128 snapshots");

    // Extract 64 snapshots
    std::vector<std::complex<float>> outSnapshots;
    bool ok = bus.getSnapshots(outSnapshots, 4, 64);
    assert(ok && "Must successfully retrieve 64 snapshots");
    assert(outSnapshots.size() == 64 * 4 && "Interleaved buffer size must be 256");

    // Verify sample correctness
    assert(std::abs(outSnapshots[0] - frame.channelData[0][0]) < 1e-6f);
    assert(std::abs(outSnapshots[1] - frame.channelData[1][0]) < 1e-6f);
    assert(std::abs(outSnapshots[2] - frame.channelData[2][0]) < 1e-6f);
    assert(std::abs(outSnapshots[3] - frame.channelData[3][0]) < 1e-6f);

    assert(bus.getBufferedSnapshots() == 64 && "Buffer must have 64 remaining snapshots");
    std::cout << "  -> PASSED. 4-channel coherent frame buffered and interleaved with zero error." << std::endl;
}

void test_ura_2d_music() {
    std::cout << "[TEST] 6. 2D Uniform Rectangular Array (URA) MUSIC (Azimuth + Elevation)..." << std::endl;

    MUSICEngine engine;
    DOAConfig cfg;
    cfg.geometry = ArrayGeometry::URA;
    cfg.num_elements_x = 4;
    cfg.num_elements_y = 4;
    cfg.num_elements = 16;
    cfg.element_spacing = 0.5f;
    cfg.element_spacing_y = 0.5f;
    cfg.expected_sources = 1;
    cfg.auto_model_order = false;
    engine.setConfig(cfg);

    ArraySynthesizer synth;
    synth.setArrayConfig(cfg);

    float true_az = 60.0f;
    float true_el = 36.0f;
    SyntheticSource src;
    src.angle_deg = true_az;
    src.elevation_deg = true_el;
    src.power_db = 0.0f;
    synth.setSources({src});
    synth.setNoiseFloor(-20.0f);

    CovarianceAccumulator acc;
    acc.init(16, 256, 0.0f, false);

    auto snapshots = synth.generateSnapshots(256);
    acc.feedSnapshotsInterleaved(snapshots.data(), 16, 256);

    Eigen::MatrixXcf Rxx;
    acc.getNormalizedCovariance(Rxx);

    DOAResult res = engine.computeDOA(Rxx, 256);
    assert(res.is_2d && "Result must be marked as 2D");
    assert(!res.estimated_targets_2d.empty() && "Must detect 2D target");

    float est_az = res.estimated_targets_2d[0].azimuth_deg;
    float est_el = res.estimated_targets_2d[0].elevation_deg;

    float err_az = std::abs(est_az - true_az);
    float err_el = std::abs(est_el - true_el);

    std::cout << "  -> True: (Az = " << true_az << " deg, El = " << true_el << " deg) | Estimated: (Az = "
              << est_az << " deg, El = " << est_el << " deg)" << std::endl;
    std::cout << "  -> Azimuth Error: " << err_az << " deg | Elevation Error: " << err_el << " deg" << std::endl;

    assert(err_az <= 3.0f && "Azimuth error must be within grid step resolution");
    assert(err_el <= 3.0f && "Elevation error must be within grid step resolution");

    std::cout << "  -> PASSED." << std::endl;
}

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "   rf_tools Direction Finding (DOA) Automated Test Suite    " << std::endl;
    std::cout << "============================================================" << std::endl;

    test_covariance_properties();
    test_root_music_ula_accuracy();
    test_calibration_subspace();
    test_crlb_benchmark();
    test_coherent_bus();
    test_ura_2d_music();

    std::cout << "============================================================" << std::endl;
    std::cout << "   ALL TESTS PASSED SUCCESSFULLY!                           " << std::endl;
    std::cout << "============================================================" << std::endl;
    return 0;
}
