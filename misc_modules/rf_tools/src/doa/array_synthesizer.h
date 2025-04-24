#pragma once
#include "types.h"
#include <vector>
#include <complex>
#include <random>

namespace rf_tools {
namespace doa {

struct SyntheticSource {
    float angle_deg = 45.0f;
    float power_db = 0.0f;
    float freq_offset_hz = 0.0f;
    bool is_coherent = false; // If true, coherently tied to source 0 (multipath reflection)
    float coherent_attenuation = 0.7f;
    float coherent_phase_deg = 30.0f;
};

class ArraySynthesizer {
public:
    ArraySynthesizer();
    ~ArraySynthesizer() = default;

    void setArrayConfig(const DOAConfig& config);
    void setSources(const std::vector<SyntheticSource>& sources);
    void setNoiseFloor(float noise_dbfs);
    void setHardwareMismatches(const std::vector<float>& gain_errors_db, const std::vector<float>& phase_errors_deg);

    // Generate K snapshots of M channels
    // Output layout: interleaved [s0_ch0, s0_ch1.. s0_chM-1, s1_ch0..]
    std::vector<std::complex<float>> generateSnapshots(size_t num_snapshots, float sample_rate = 2000000.0f);

private:
    DOAConfig config_;
    std::vector<SyntheticSource> sources_;
    float noise_power_linear_{1e-3f};

    std::vector<float> gain_mismatches_;
    std::vector<float> phase_mismatches_rad_;
    bool has_mismatches_{false};

    std::mt19937 rng_;
    std::normal_distribution<float> normal_dist_;
    uint64_t sample_index_{0};
};

} // namespace doa
} // namespace rf_tools
