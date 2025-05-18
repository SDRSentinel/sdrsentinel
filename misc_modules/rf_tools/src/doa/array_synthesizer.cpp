#include "array_synthesizer.h"
#include <cmath>

namespace rf_tools {
namespace doa {

static constexpr float PI = 3.14159265358979323846f;
static constexpr float DEG_TO_RAD = PI / 180.0f;

ArraySynthesizer::ArraySynthesizer()
    : rng_(1337), normal_dist_(0.0f, 1.0f) {
    config_ = DOAConfig{};
    sources_.push_back({ 45.0f, 0.0f, 0.0f, false, 0.7f, 30.0f, 20.0f });
    setNoiseFloor(-30.0f);
}

void ArraySynthesizer::setArrayConfig(const DOAConfig& config) {
    config_ = config;
}

void ArraySynthesizer::setSources(const std::vector<SyntheticSource>& sources) {
    sources_ = sources;
}

void ArraySynthesizer::setNoiseFloor(float noise_dbfs) {
    noise_power_linear_ = std::pow(10.0f, noise_dbfs / 10.0f);
}

void ArraySynthesizer::setHardwareMismatches(const std::vector<float>& gain_errors_db, const std::vector<float>& phase_errors_deg) {
    const size_t M = config_.num_elements;
    gain_mismatches_.resize(M, 1.0f);
    phase_mismatches_rad_.resize(M, 0.0f);

    for (size_t i = 0; i < std::min(M, gain_errors_db.size()); ++i) {
        gain_mismatches_[i] = std::pow(10.0f, gain_errors_db[i] / 20.0f);
    }
    for (size_t i = 0; i < std::min(M, phase_errors_deg.size()); ++i) {
        phase_mismatches_rad_[i] = phase_errors_deg[i] * DEG_TO_RAD;
    }
    has_mismatches_ = true;
}

std::vector<std::complex<float>> ArraySynthesizer::generateSnapshots(size_t num_snapshots, float sample_rate) {
    const size_t M = config_.num_elements;
    std::vector<std::complex<float>> output(num_snapshots * M, 0.0f);

    // Precalculate steering vectors for each source
    std::vector<std::vector<std::complex<float>>> steering_vectors(sources_.size(), std::vector<std::complex<float>>(M));
    for (size_t s = 0; s < sources_.size(); ++s) {
        float theta_rad = sources_[s].angle_deg * DEG_TO_RAD;

        if (config_.geometry == ArrayGeometry::ULA) {
            for (size_t m = 0; m < M; ++m) {
                float loc = config_.element_spacing * 0.5f * (static_cast<float>(M) - 1.0f - 2.0f * static_cast<float>(m));
                float phase = -2.0f * PI * std::cos(theta_rad) * loc;
                steering_vectors[s][m] = std::complex<float>(std::cos(phase), std::sin(phase));
            }
        } else if (config_.geometry == ArrayGeometry::URA) {
            size_t Mx = config_.num_elements_x > 0 ? config_.num_elements_x : 2;
            size_t My = config_.num_elements_y > 0 ? config_.num_elements_y : 2;
            float dx = config_.element_spacing;
            float dy = config_.element_spacing_y > 0.0f ? config_.element_spacing_y : config_.element_spacing;
            float el_rad = sources_[s].elevation_deg * DEG_TO_RAD;
            float u = std::cos(el_rad) * std::cos(theta_rad);
            float v = std::cos(el_rad) * std::sin(theta_rad);

            for (size_t my = 0; my < My; ++my) {
                for (size_t mx = 0; mx < Mx; ++mx) {
                    size_t m = my * Mx + mx;
                    if (m < M) {
                        float phase = -2.0f * PI * (static_cast<float>(mx) * dx * u + static_cast<float>(my) * dy * v);
                        steering_vectors[s][m] = std::complex<float>(std::cos(phase), std::sin(phase));
                    }
                }
            }
        } else {
            float radius = config_.element_spacing / (2.0f * std::sin(PI / static_cast<float>(M)));
            for (size_t m = 0; m < M; ++m) {
                float phi_m = 2.0f * PI * static_cast<float>(m) / static_cast<float>(M);
                float phase = -2.0f * PI * radius * std::cos(theta_rad - phi_m);
                steering_vectors[s][m] = std::complex<float>(std::cos(phase), std::sin(phase));
            }
        }
    }

    float noise_std = std::sqrt(noise_power_linear_ / 2.0f);

    for (size_t n = 0; n < num_snapshots; ++n) {
        float t = static_cast<float>(sample_index_++) / sample_rate;

        // Base signal for source 0 (used for coherent multipath)
        std::complex<float> s0(0.0f, 0.0f);

        for (size_t s = 0; s < sources_.size(); ++s) {
            float amp = std::pow(10.0f, sources_[s].power_db / 20.0f);
            std::complex<float> signal_val;

            if (s == 0) {
                float phase = 2.0f * PI * sources_[s].freq_offset_hz * t;
                s0 = amp * std::complex<float>(std::cos(phase), std::sin(phase));
                signal_val = s0;
            } else if (sources_[s].is_coherent) {
                // Coherent multipath: phase shifted and attenuated version of source 0
                float multipath_phase = sources_[s].coherent_phase_deg * DEG_TO_RAD;
                std::complex<float> rot(std::cos(multipath_phase), std::sin(multipath_phase));
                signal_val = s0 * sources_[s].coherent_attenuation * rot;
            } else {
                float phase = 2.0f * PI * sources_[s].freq_offset_hz * t + static_cast<float>(s) * 1.57f;
                signal_val = amp * std::complex<float>(std::cos(phase), std::sin(phase));
            }

            for (size_t m = 0; m < M; ++m) {
                output[n * M + m] += signal_val * steering_vectors[s][m];
            }
        }

        // Apply hardware mismatches (gain/phase) and additive white Gaussian noise (AWGN)
        for (size_t m = 0; m < M; ++m) {
            if (has_mismatches_ && m < gain_mismatches_.size()) {
                float gain = gain_mismatches_[m];
                float phase = phase_mismatches_rad_[m];
                std::complex<float> mismatch_factor = gain * std::complex<float>(std::cos(phase), std::sin(phase));
                output[n * M + m] *= mismatch_factor;
            }

            std::complex<float> noise(normal_dist_(rng_) * noise_std, normal_dist_(rng_) * noise_std);
            output[n * M + m] += noise;
        }
    }

    return output;
}

} // namespace doa
} // namespace rf_tools
