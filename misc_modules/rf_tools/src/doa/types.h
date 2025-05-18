#pragma once
#include <Eigen/Dense>
#include <complex>
#include <vector>
#include <cstddef>
#include <cstdint>

namespace rf_tools {
namespace doa {

enum class ArrayGeometry {
    ULA, // Uniform Linear Array (1D)
    UCA, // Uniform Circular Array (Azimuth)
    URA  // Uniform Rectangular Array (2D Azimuth + Elevation)
};

struct Target2D {
    float azimuth_deg = 0.0f;
    float elevation_deg = 0.0f;
    float power_db = 0.0f;
};

struct DOAConfig {
    ArrayGeometry geometry = ArrayGeometry::ULA;
    size_t num_elements = 4;
    size_t num_elements_x = 2; // For URA: elements along X
    size_t num_elements_y = 2; // For URA: elements along Y
    float element_spacing = 0.5f; // In units of wavelength (d / lambda)
    float element_spacing_y = 0.5f; // For URA (dy / lambda)
    size_t num_snapshots = 256;
    float forgetting_factor = 0.0f; // 0.0: block average, >0.0 (e.g. 0.98): recursive EMA
    bool enable_fbss = false; // Forward-Backward Spatial Smoothing
    size_t expected_sources = 1;
    bool auto_model_order = true; // MDL model order estimation
    float scan_resolution_deg = 0.5f; // Step size for pseudo-spectrum scanning
};

struct DOAResult {
    std::vector<float> estimated_angles_deg; // Detected angles (1D: Azimuth)
    std::vector<Target2D> estimated_targets_2d; // Detected targets in 2D (Azimuth + Elevation)
    std::vector<float> spectrum_angles_deg;  // Angle grid for 1D plot
    std::vector<float> pseudo_spectrum_db;   // Normalized MUSIC pseudo-spectrum in dB

    // 2D Spatial Heatmap Grid
    bool is_2d = false;
    std::vector<float> spectrum_azimuths_deg;
    std::vector<float> spectrum_elevations_deg;
    std::vector<float> pseudo_spectrum_2d_db; // [elevation_idx * num_azimuths + azimuth_idx]
    float min_2d_db = -40.0f;
    float max_2d_db = 0.0f;

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
