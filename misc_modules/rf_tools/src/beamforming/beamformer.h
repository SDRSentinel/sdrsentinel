#pragma once
#include <complex>
#include <vector>
#include <Eigen/Dense>
#include "doa/types.h"

namespace rf_tools {
namespace beamforming {

enum class BeamformerAlgorithm {
    CONVENTIONAL,       // Delay-and-Sum (Bartlett)
    MVDR,               // Minimum Variance Distortionless Response (Capon)
    ROBUST_MVDR,        // Diagonal Loaded MVDR (LSMI)
    LCMV                // Linearly Constrained Minimum Variance (Explicit Null Steering)
};

struct BeamformerConfig {
    BeamformerAlgorithm algorithm = BeamformerAlgorithm::MVDR;
    doa::ArrayGeometry geometry = doa::ArrayGeometry::ULA;
    size_t num_elements = 4;
    float element_spacing = 0.5f;

    // 2D URA configuration
    size_t num_elements_x = 2;
    size_t num_elements_y = 2;
    float element_spacing_y = 0.5f;

    // Desired look direction
    float look_azimuth_deg = 90.0f;
    float look_elevation_deg = 0.0f;

    // Explicit null steering directions (interferers/jammers)
    std::vector<float> null_azimuths_deg;
    std::vector<float> null_elevations_deg;

    // Regularization / Diagonal loading for robustness
    float diagonal_loading_factor = 1e-3f; // Relative to Tr(Rxx)/M
    float pattern_resolution_deg = 0.5f;   // Beampattern angular grid resolution
};

struct BeamformerResult {
    Eigen::VectorXcf weights;           // M-element complex weight vector w (y = w^H x)
    float output_power = 0.0f;          // w^H Rxx w
    float look_gain_db = 0.0f;          // 20*log10(|w^H a(theta_look)|) (ideally 0 dB)
    std::vector<float> null_gains_db;   // Gains at specified null angles

    // 1D Beampattern
    std::vector<float> pattern_azimuths_deg;
    std::vector<float> pattern_gains_db;

    // 2D Beampattern (for URA)
    bool is_2d = false;
    std::vector<float> pattern_elevations_deg;
    std::vector<float> pattern_2d_db;   // num_elevations x num_azimuths
};

class AdaptiveBeamformer {
public:
    AdaptiveBeamformer();
    ~AdaptiveBeamformer() = default;

    void setConfig(const BeamformerConfig& config);
    const BeamformerConfig& getConfig() const { return config_; }

    void setLookDirection(float azimuth_deg, float elevation_deg = 0.0f);
    void setNullDirections(const std::vector<float>& null_azimuths_deg, const std::vector<float>& null_elevations_deg = {});
    void addNullDirection(float azimuth_deg, float elevation_deg = 0.0f);
    void clearNullDirections();

    // Compute optimal spatial filter weights from spatial covariance matrix Rxx
    BeamformerResult computeWeights(const Eigen::MatrixXcf& Rxx);

    // Steering vector evaluation
    Eigen::VectorXcf getSteeringVector(float azimuth_deg, float elevation_deg = 0.0f) const;

    // Spatial filter response at a specific angle
    float evaluateGainLinear(float azimuth_deg, float elevation_deg = 0.0f) const;
    float evaluateGainDb(float azimuth_deg, float elevation_deg = 0.0f) const;

    // Real-time signal filtering
    // y = w^H x = sum(w_m^* * x_m)
    std::complex<float> filterSample(const std::complex<float>* multichannel_sample) const;
    void filterInterleaved(const std::complex<float>* input, size_t num_samples, std::complex<float>* output) const;
    void filterPlanar(const std::vector<std::vector<std::complex<float>>>& input, std::vector<std::complex<float>>& output) const;

    const Eigen::VectorXcf& getWeights() const { return weights_; }
    const BeamformerResult& getLastResult() const { return last_result_; }

private:
    void computeBeampattern(BeamformerResult& result) const;
    void computeBeampattern2D(BeamformerResult& result) const;

    BeamformerConfig config_;
    Eigen::VectorXcf weights_;
    BeamformerResult last_result_;
};

} // namespace beamforming
} // namespace rf_tools
