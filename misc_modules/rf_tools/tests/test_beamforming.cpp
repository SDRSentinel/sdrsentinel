#include <iostream>
#include <iomanip>
#include <cassert>
#include <cmath>
#include <vector>
#include <complex>

#include "beamforming/beamformer.h"
#include "doa/array_synthesizer.h"
#include "doa/covariance_accumulator.h"

using namespace rf_tools;
using namespace rf_tools::beamforming;
using namespace rf_tools::doa;

void test_bartlett_beamformer() {
    std::cout << "[TEST] 1. Delay-and-Sum (Bartlett) Beamformer & Array Gain..." << std::endl;

    const size_t M = 8;
    BeamformerConfig cfg;
    cfg.algorithm = BeamformerAlgorithm::CONVENTIONAL;
    cfg.geometry = ArrayGeometry::ULA;
    cfg.num_elements = M;
    cfg.element_spacing = 0.5f;
    cfg.look_azimuth_deg = 90.0f;

    AdaptiveBeamformer bf;
    bf.setConfig(cfg);

    // Identity covariance (spatially white noise)
    Eigen::MatrixXcf Rxx = Eigen::MatrixXcf::Identity(M, M);
    BeamformerResult res = bf.computeWeights(Rxx);

    float look_gain = bf.evaluateGainDb(90.0f);
    std::cout << "  -> Look direction gain at 90.0 deg: " << look_gain << " dB" << std::endl;
    assert(std::abs(look_gain) < 0.05f && "Look direction gain must be unity (0 dB)");

    // Output noise power = w^H * I * w = ||w||^2 = 1 / M
    // Sidelobe at 40 deg
    float sidelobe_gain = bf.evaluateGainDb(40.0f);
    std::cout << "  -> Sidelobe gain at 40.0 deg: " << sidelobe_gain << " dB" << std::endl;
    assert(sidelobe_gain < -10.0f && "Sidelobe must be well below mainlobe");

    std::cout << "  -> PASSED." << std::endl;
}

void test_mvdr_null_steering() {
    std::cout << "[TEST] 2. Adaptive MVDR (Capon) Jammer Suppression..." << std::endl;

    const size_t M = 8;
    const size_t K = 512;
    const float look_az = 90.0f;
    const float jammer_az = 50.0f;

    DOAConfig array_cfg;
    array_cfg.geometry = ArrayGeometry::ULA;
    array_cfg.num_elements = M;
    array_cfg.element_spacing = 0.5f;

    ArraySynthesizer synth;
    synth.setArrayConfig(array_cfg);

    // SOI: 90 deg, 0 dB power
    // High-power Jammer: 50 deg, +25 dB power (25 dB stronger than SOI!)
    SyntheticSource soi;
    soi.angle_deg = look_az;
    soi.power_db = 0.0f;
    soi.freq_offset_hz = 1000.0f;

    SyntheticSource jammer;
    jammer.angle_deg = jammer_az;
    jammer.power_db = 25.0f;
    jammer.freq_offset_hz = 25000.0f;

    synth.setSources({soi, jammer});
    synth.setNoiseFloor(-25.0f);

    CovarianceAccumulator acc;
    acc.init(M, K, 0.0f, false);

    auto snapshots = synth.generateSnapshots(K);
    acc.feedSnapshotsInterleaved(snapshots.data(), M, K);

    Eigen::MatrixXcf Rxx;
    acc.getNormalizedCovariance(Rxx);

    BeamformerConfig bf_cfg;
    bf_cfg.algorithm = BeamformerAlgorithm::MVDR;
    bf_cfg.geometry = ArrayGeometry::ULA;
    bf_cfg.num_elements = M;
    bf_cfg.element_spacing = 0.5f;
    bf_cfg.look_azimuth_deg = look_az;
    bf_cfg.diagonal_loading_factor = 1e-4f;

    AdaptiveBeamformer bf;
    bf.setConfig(bf_cfg);
    BeamformerResult res = bf.computeWeights(Rxx);

    float gain_soi = bf.evaluateGainDb(look_az);
    float gain_jammer = bf.evaluateGainDb(jammer_az);

    std::cout << "  -> Look Direction Gain (SOI at " << look_az << " deg): " << gain_soi << " dB" << std::endl;
    std::cout << "  -> Jammer Direction Gain (Jammer at " << jammer_az << " deg): " << gain_jammer << " dB" << std::endl;

    assert(std::abs(gain_soi) < 0.1f && "SOI gain must remain undistorted (0 dB)");
    assert(gain_jammer < -25.0f && "MVDR must automatically place deep spatial null at jammer angle");

    float suppression_db = gain_soi - gain_jammer;
    std::cout << "  -> Total Relative Jammer Suppression: " << suppression_db << " dB" << std::endl;
    assert(suppression_db > 25.0f && "Jammer suppression must exceed 25 dB");

    std::cout << "  -> PASSED." << std::endl;
}

void test_lcmv_explicit_nulls() {
    std::cout << "[TEST] 3. LCMV Multi-Null Steering (Mathematical Exact Nulls)..." << std::endl;

    const size_t M = 8;
    const float look_az = 80.0f;
    const float null1_az = 35.0f;
    const float null2_az = 125.0f;

    BeamformerConfig cfg;
    cfg.algorithm = BeamformerAlgorithm::LCMV;
    cfg.geometry = ArrayGeometry::ULA;
    cfg.num_elements = M;
    cfg.element_spacing = 0.5f;
    cfg.look_azimuth_deg = look_az;
    cfg.null_azimuths_deg = { null1_az, null2_az };

    AdaptiveBeamformer bf;
    bf.setConfig(cfg);

    Eigen::MatrixXcf Rxx = Eigen::MatrixXcf::Identity(M, M);
    BeamformerResult res = bf.computeWeights(Rxx);

    float gain_look = bf.evaluateGainDb(look_az);
    float gain_null1 = bf.evaluateGainDb(null1_az);
    float gain_null2 = bf.evaluateGainDb(null2_az);

    std::cout << "  -> Look Direction (80.0 deg): " << gain_look << " dB" << std::endl;
    std::cout << "  -> Explicit Null 1 (35.0 deg): " << gain_null1 << " dB" << std::endl;
    std::cout << "  -> Explicit Null 2 (125.0 deg): " << gain_null2 << " dB" << std::endl;

    assert(std::abs(gain_look) < 0.05f && "Look direction must be exactly 0 dB");
    assert(gain_null1 < -50.0f && "Null 1 must be constrained to < -50 dB");
    assert(gain_null2 < -50.0f && "Null 2 must be constrained to < -50 dB");

    std::cout << "  -> PASSED." << std::endl;
}

void test_stream_filtering_sinr() {
    std::cout << "[TEST] 4. Multi-Channel Stream Filtering & SINR Enhancement..." << std::endl;

    const size_t M = 4;
    const size_t num_samples = 2048;

    DOAConfig array_cfg;
    array_cfg.geometry = ArrayGeometry::ULA;
    array_cfg.num_elements = M;
    array_cfg.element_spacing = 0.5f;

    ArraySynthesizer synth;
    synth.setArrayConfig(array_cfg);

    SyntheticSource soi;
    soi.angle_deg = 90.0f;
    soi.power_db = 0.0f; // 0 dB
    soi.freq_offset_hz = 5000.0f;

    SyntheticSource jammer;
    jammer.angle_deg = 40.0f;
    jammer.power_db = 20.0f; // +20 dB jammer (100x stronger!)
    jammer.freq_offset_hz = 50000.0f;

    synth.setSources({soi, jammer});
    synth.setNoiseFloor(-30.0f);

    auto raw_multichannel = synth.generateSnapshots(num_samples);

    // Compute input power on Channel 0
    float pwr_ch0 = 0.0f;
    for (size_t i = 0; i < num_samples; ++i) {
        pwr_ch0 += std::norm(raw_multichannel[i * M]);
    }
    pwr_ch0 /= static_cast<float>(num_samples);

    // Covariance estimation
    CovarianceAccumulator acc;
    acc.init(M, num_samples, 0.0f, false);
    acc.feedSnapshotsInterleaved(raw_multichannel.data(), M, num_samples);
    Eigen::MatrixXcf Rxx;
    acc.getNormalizedCovariance(Rxx);

    // MVDR Beamformer looking at SOI (90 deg)
    BeamformerConfig bf_cfg;
    bf_cfg.algorithm = BeamformerAlgorithm::MVDR;
    bf_cfg.geometry = ArrayGeometry::ULA;
    bf_cfg.num_elements = M;
    bf_cfg.look_azimuth_deg = 90.0f;

    AdaptiveBeamformer bf;
    bf.setConfig(bf_cfg);
    bf.computeWeights(Rxx);

    // Filter stream
    std::vector<std::complex<float>> filtered_stream(num_samples);
    bf.filterInterleaved(raw_multichannel.data(), num_samples, filtered_stream.data());

    float pwr_out = 0.0f;
    for (size_t i = 0; i < num_samples; ++i) {
        pwr_out += std::norm(filtered_stream[i]);
    }
    pwr_out /= static_cast<float>(num_samples);

    float suppression_ratio_db = 10.0f * std::log10(pwr_ch0 / pwr_out);
    std::cout << "  -> Channel 0 Input Power: " << 10.0f * std::log10(pwr_ch0) << " dBFS" << std::endl;
    std::cout << "  -> Beamformer Output Power: " << 10.0f * std::log10(pwr_out) << " dBFS" << std::endl;
    std::cout << "  -> Total Interference Reduction: " << suppression_ratio_db << " dB" << std::endl;

    assert(suppression_ratio_db > 15.0f && "Interference power must be significantly reduced");
    std::cout << "  -> PASSED." << std::endl;
}

void test_2d_ura_beamformer() {
    std::cout << "[TEST] 5. 2D Uniform Rectangular Array (URA) Beamforming..." << std::endl;

    const size_t Mx = 4;
    const size_t My = 4;
    const size_t M = Mx * My;

    BeamformerConfig cfg;
    cfg.algorithm = BeamformerAlgorithm::LCMV;
    cfg.geometry = ArrayGeometry::URA;
    cfg.num_elements = M;
    cfg.num_elements_x = Mx;
    cfg.num_elements_y = My;
    cfg.element_spacing = 0.5f;
    cfg.element_spacing_y = 0.5f;

    // Look direction: Az = 60 deg, El = 30 deg
    cfg.look_azimuth_deg = 60.0f;
    cfg.look_elevation_deg = 30.0f;

    // Null direction: Az = 140 deg, El = 45 deg
    cfg.null_azimuths_deg = { 140.0f };
    cfg.null_elevations_deg = { 45.0f };

    AdaptiveBeamformer bf;
    bf.setConfig(cfg);

    Eigen::MatrixXcf Rxx = Eigen::MatrixXcf::Identity(M, M);
    BeamformerResult res = bf.computeWeights(Rxx);

    float gain_look = bf.evaluateGainDb(60.0f, 30.0f);
    float gain_null = bf.evaluateGainDb(140.0f, 45.0f);

    std::cout << "  -> 2D Look (Az=60, El=30): " << gain_look << " dB" << std::endl;
    std::cout << "  -> 2D Null (Az=140, El=45): " << gain_null << " dB" << std::endl;

    assert(std::abs(gain_look) < 0.05f && "2D look direction must be unity gain (0 dB)");
    assert(gain_null < -50.0f && "2D null direction must be constrained to < -50 dB");
    assert(res.is_2d && "Result must include 2D beampattern");
    assert(!res.pattern_2d_db.empty() && "2D pattern matrix must be populated");

    std::cout << "  -> PASSED." << std::endl;
}

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "   rf_tools Adaptive Beamforming Automated Test Suite       " << std::endl;
    std::cout << "============================================================" << std::endl;

    test_bartlett_beamformer();
    test_mvdr_null_steering();
    test_lcmv_explicit_nulls();
    test_stream_filtering_sinr();
    test_2d_ura_beamformer();

    std::cout << "============================================================" << std::endl;
    std::cout << "   ALL BEAMFORMING TESTS PASSED SUCCESSFULLY!               " << std::endl;
    std::cout << "============================================================" << std::endl;
    return 0;
}
