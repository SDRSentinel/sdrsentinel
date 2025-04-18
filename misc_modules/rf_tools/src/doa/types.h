#pragma once
#include <Eigen/Dense>
#include <complex>
#include <vector>
#include <cstddef>

namespace rf_tools {
namespace doa {

enum class ArrayGeometry {
    ULA, // Uniform Linear Array
    UCA  // Uniform Circular Array
};

struct DOAConfig {
    ArrayGeometry geometry = ArrayGeometry::ULA;
    size_t num_elements = 4;
    float element_spacing = 0.5f; // In units of wavelength (d / lambda)
    size_t num_snapshots = 256;
    float forgetting_factor = 0.0f; // 0.0: block average, >0.0 (e.g. 0.98): recursive EMA
    bool enable_fbss = false; // Forward-Backward Spatial Smoothing
    size_t expected_sources = 1;
    bool auto_model_order = true; // MDL model order estimation
    float scan_resolution_deg = 0.5f; // Step size for pseudo-spectrum scanning
};

struct DOAResult {
    std::vector<float> estimated_angles_deg; // Detected angles (peak of MUSIC or Root-MUSIC roots)
    std::vector<float> spectrum_angles_deg;  // Angle grid for plot
    std::vector<float> pseudo_spectrum_db;   // Normalized MUSIC pseudo-spectrum in dB
    size_t model_order = 1;
    uint64_t total_snapshots = 0;
    double timestamp = 0.0;
};

// Aligned fixed-size and dynamic matrix aliases
using MatrixXcf = Eigen::MatrixXcf;
using VectorXcf = Eigen::VectorXcf;
using VectorXf = Eigen::VectorXf;

} // namespace doa
} // namespace rf_tools
