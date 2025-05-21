#include "beamformer.h"
#include <cmath>
#include <algorithm>

namespace rf_tools {
namespace beamforming {

static constexpr float PI = 3.14159265358979323846f;
static constexpr float DEG_TO_RAD = PI / 180.0f;
static constexpr float RAD_TO_DEG = 180.0f / PI;

AdaptiveBeamformer::AdaptiveBeamformer() {
    config_ = BeamformerConfig{};
    weights_ = Eigen::VectorXcf::Zero(config_.num_elements);
}

void AdaptiveBeamformer::setConfig(const BeamformerConfig& config) {
    config_ = config;
    if (weights_.size() != static_cast<Eigen::Index>(config_.num_elements)) {
        weights_ = Eigen::VectorXcf::Zero(config_.num_elements);
    }
}

void AdaptiveBeamformer::setLookDirection(float azimuth_deg, float elevation_deg) {
    config_.look_azimuth_deg = azimuth_deg;
    config_.look_elevation_deg = elevation_deg;
}

void AdaptiveBeamformer::setNullDirections(const std::vector<float>& null_azimuths_deg, const std::vector<float>& null_elevations_deg) {
    config_.null_azimuths_deg = null_azimuths_deg;
    config_.null_elevations_deg = null_elevations_deg;
}

void AdaptiveBeamformer::addNullDirection(float azimuth_deg, float elevation_deg) {
    config_.null_azimuths_deg.push_back(azimuth_deg);
    config_.null_elevations_deg.push_back(elevation_deg);
}

void AdaptiveBeamformer::clearNullDirections() {
    config_.null_azimuths_deg.clear();
    config_.null_elevations_deg.clear();
}

Eigen::VectorXcf AdaptiveBeamformer::getSteeringVector(float azimuth_deg, float elevation_deg) const {
    const size_t M = config_.num_elements;
    Eigen::VectorXcf a(M);

    if (config_.geometry == doa::ArrayGeometry::ULA) {
        float theta_rad = azimuth_deg * DEG_TO_RAD;
        for (size_t m = 0; m < M; ++m) {
            float loc = config_.element_spacing * 0.5f * (static_cast<float>(M) - 1.0f - 2.0f * static_cast<float>(m));
            float phase = -2.0f * PI * std::cos(theta_rad) * loc;
            a(m) = std::complex<float>(std::cos(phase), std::sin(phase));
        }
    } else if (config_.geometry == doa::ArrayGeometry::UCA) {
        float theta_rad = azimuth_deg * DEG_TO_RAD;
        float radius = config_.element_spacing / (2.0f * std::sin(PI / static_cast<float>(M)));
        for (size_t m = 0; m < M; ++m) {
            float elem_angle = 2.0f * PI * static_cast<float>(m) / static_cast<float>(M);
            float phase = 2.0f * PI * radius * std::cos(theta_rad - elem_angle);
            a(m) = std::complex<float>(std::cos(phase), std::sin(phase));
        }
    } else if (config_.geometry == doa::ArrayGeometry::URA) {
        size_t Mx = config_.num_elements_x > 0 ? config_.num_elements_x : 2;
        size_t My = config_.num_elements_y > 0 ? config_.num_elements_y : 2;
        float dx = config_.element_spacing;
        float dy = config_.element_spacing_y > 0.0f ? config_.element_spacing_y : dx;

        float az_rad = azimuth_deg * DEG_TO_RAD;
        float el_rad = elevation_deg * DEG_TO_RAD;
        float cos_el = std::cos(el_rad);
        float u = cos_el * std::cos(az_rad);
        float v = cos_el * std::sin(az_rad);

        for (size_t my = 0; my < My; ++my) {
            for (size_t mx = 0; mx < Mx; ++mx) {
                size_t idx = my * Mx + mx;
                if (idx < M) {
                    float phase = -2.0f * PI * (static_cast<float>(mx) * dx * u + static_cast<float>(my) * dy * v);
                    a(idx) = std::complex<float>(std::cos(phase), std::sin(phase));
                }
            }
        }
    }
    return a;
}

float AdaptiveBeamformer::evaluateGainLinear(float azimuth_deg, float elevation_deg) const {
    if (weights_.size() == 0) return 0.0f;
    Eigen::VectorXcf a = getSteeringVector(azimuth_deg, elevation_deg);
    std::complex<float> response = weights_.adjoint() * a;
    return std::abs(response);
}

float AdaptiveBeamformer::evaluateGainDb(float azimuth_deg, float elevation_deg) const {
    float gain_lin = evaluateGainLinear(azimuth_deg, elevation_deg);
    return 20.0f * std::log10(std::max(gain_lin, 1e-6f));
}

BeamformerResult AdaptiveBeamformer::computeWeights(const Eigen::MatrixXcf& Rxx) {
    BeamformerResult result;
    const size_t M = Rxx.rows();
    if (M < 2) return result;

    config_.num_elements = M;
    Eigen::VectorXcf a0 = getSteeringVector(config_.look_azimuth_deg, config_.look_elevation_deg);

    float trace_pwr = Rxx.trace().real();
    float avg_pwr = trace_pwr / static_cast<float>(M);
    float loading = std::max(config_.diagonal_loading_factor * avg_pwr, 1e-6f);

    Eigen::MatrixXcf R_loaded = Rxx;
    for (size_t m = 0; m < M; ++m) {
        R_loaded(m, m) += std::complex<float>(loading, 0.0f);
    }

    if (config_.algorithm == BeamformerAlgorithm::CONVENTIONAL) {
        // Delay-and-Sum (Bartlett) beamformer: w = a0 / M
        weights_ = a0 / static_cast<float>(M);

    } else if (config_.algorithm == BeamformerAlgorithm::MVDR ||
               config_.algorithm == BeamformerAlgorithm::ROBUST_MVDR) {
        // MVDR (Capon) beamformer: w = (R^-1 * a0) / (a0^H * R^-1 * a0)
        Eigen::ColPivHouseholderQR<Eigen::MatrixXcf> qr(R_loaded);
        Eigen::VectorXcf v = qr.solve(a0);
        std::complex<float> denom = a0.adjoint() * v;
        float denom_real = std::max(denom.real(), 1e-12f);
        weights_ = v / denom_real;

    } else if (config_.algorithm == BeamformerAlgorithm::LCMV) {
        // Linearly Constrained Minimum Variance (LCMV) beamformer:
        // C^H * w = f
        // w = R^-1 * C * (C^H * R^-1 * C)^-1 * f
        size_t num_nulls = config_.null_azimuths_deg.size();
        size_t total_constraints = std::min(1 + num_nulls, M);

        Eigen::MatrixXcf C(M, total_constraints);
        Eigen::VectorXcf f = Eigen::VectorXcf::Zero(total_constraints);

        // Constraint 0: Unity gain at look direction
        C.col(0) = a0;
        f(0) = std::complex<float>(1.0f, 0.0f);

        // Constraints 1..P-1: Exact nulls at specified interference angles
        for (size_t j = 0; j < total_constraints - 1; ++j) {
            float null_el = (j < config_.null_elevations_deg.size()) ? config_.null_elevations_deg[j] : 0.0f;
            C.col(j + 1) = getSteeringVector(config_.null_azimuths_deg[j], null_el);
            f(j + 1) = std::complex<float>(0.0f, 0.0f);
        }

        Eigen::ColPivHouseholderQR<Eigen::MatrixXcf> qr(R_loaded);
        Eigen::MatrixXcf Rinv_C = qr.solve(C); // M x P

        Eigen::MatrixXcf Gramian = C.adjoint() * Rinv_C; // P x P
        Eigen::ColPivHouseholderQR<Eigen::MatrixXcf> qr_gram(Gramian);
        Eigen::VectorXcf lambda = qr_gram.solve(f); // P x 1

        weights_ = Rinv_C * lambda;
    }

    result.weights = weights_;

    // Calculate output array power: w^H * Rxx * w
    std::complex<float> out_pwr_c = weights_.adjoint() * Rxx * weights_;
    result.output_power = std::max(out_pwr_c.real(), 0.0f);

    // Calculate gain at look direction
    result.look_gain_db = evaluateGainDb(config_.look_azimuth_deg, config_.look_elevation_deg);

    // Calculate gains at null directions
    result.null_gains_db.resize(config_.null_azimuths_deg.size());
    for (size_t i = 0; i < config_.null_azimuths_deg.size(); ++i) {
        float el = (i < config_.null_elevations_deg.size()) ? config_.null_elevations_deg[i] : 0.0f;
        result.null_gains_db[i] = evaluateGainDb(config_.null_azimuths_deg[i], el);
    }

    // Compute beampattern
    computeBeampattern(result);
    if (config_.geometry == doa::ArrayGeometry::URA) {
        computeBeampattern2D(result);
    }

    last_result_ = result;
    return result;
}

void AdaptiveBeamformer::computeBeampattern(BeamformerResult& result) const {
    float res_deg = std::max(config_.pattern_resolution_deg, 0.1f);
    float max_az = (config_.geometry == doa::ArrayGeometry::ULA) ? 180.0f : 360.0f;
    size_t num_points = static_cast<size_t>(max_az / res_deg) + 1;

    result.pattern_azimuths_deg.resize(num_points);
    result.pattern_gains_db.resize(num_points);

    for (size_t i = 0; i < num_points; ++i) {
        float az = static_cast<float>(i) * res_deg;
        result.pattern_azimuths_deg[i] = az;
        result.pattern_gains_db[i] = evaluateGainDb(az, config_.look_elevation_deg);
    }
}

void AdaptiveBeamformer::computeBeampattern2D(BeamformerResult& result) const {
    result.is_2d = true;
    float az_step = 2.0f;
    float el_step = 2.0f;

    size_t num_az = static_cast<size_t>(360.0f / az_step) + 1;
    size_t num_el = static_cast<size_t>(90.0f / el_step) + 1;

    result.pattern_elevations_deg.resize(num_el);
    for (size_t e = 0; e < num_el; ++e) {
        result.pattern_elevations_deg[e] = static_cast<float>(e) * el_step;
    }

    result.pattern_2d_db.resize(num_el * num_az);
    for (size_t e = 0; e < num_el; ++e) {
        float el = result.pattern_elevations_deg[e];
        for (size_t a = 0; a < num_az; ++a) {
            float az = static_cast<float>(a) * az_step;
            result.pattern_2d_db[e * num_az + a] = evaluateGainDb(az, el);
        }
    }
}

std::complex<float> AdaptiveBeamformer::filterSample(const std::complex<float>* multichannel_sample) const {
    const size_t M = weights_.size();
    std::complex<float> out(0.0f, 0.0f);
    for (size_t m = 0; m < M; ++m) {
        out += std::conj(weights_(m)) * multichannel_sample[m];
    }
    return out;
}

void AdaptiveBeamformer::filterInterleaved(const std::complex<float>* input, size_t num_samples, std::complex<float>* output) const {
    const size_t M = weights_.size();
    for (size_t n = 0; n < num_samples; ++n) {
        output[n] = filterSample(&input[n * M]);
    }
}

void AdaptiveBeamformer::filterPlanar(const std::vector<std::vector<std::complex<float>>>& input, std::vector<std::complex<float>>& output) const {
    if (input.empty()) return;
    const size_t M = weights_.size();
    const size_t num_samples = input[0].size();
    output.resize(num_samples, std::complex<float>(0.0f, 0.0f));

    for (size_t n = 0; n < num_samples; ++n) {
        std::complex<float> out(0.0f, 0.0f);
        for (size_t m = 0; m < std::min(M, input.size()); ++m) {
            out += std::conj(weights_(m)) * input[m][n];
        }
        output[n] = out;
    }
}

} // namespace beamforming
} // namespace rf_tools
