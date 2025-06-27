#pragma once
// See music_engine.cpp: portions of this engine are translated from gr-doa, https://github.com/EttusResearch/gr-doa (GPLv3).
// Copyright 2016 Srikanth Pagadarai <srikanth.pagadarai@gmail.com>,
// Travis F. Collins <travisfcollins@gmail.com>
// The pilot calibration follows Soon et al. 1994.

#include "types.h"
#include <vector>
#include <complex>

namespace rf_tools {
namespace doa {

class MUSICEngine {
public:
    MUSICEngine();
    ~MUSICEngine() = default;

    void setConfig(const DOAConfig& config);
    const DOAConfig& getConfig() const { return config_; }

    // Estimate model order (number of active sources) using Minimum Description Length (MDL)
    size_t estimateModelOrderMDL(const Eigen::VectorXf& eigenvalues_desc, size_t num_snapshots) const;

    // Estimate model order using Akaike Information Criterion (AIC)
    size_t estimateModelOrderAIC(const Eigen::VectorXf& eigenvalues_desc, size_t num_snapshots) const;

    // Compute Spectral MUSIC pseudo-spectrum and extract peaks (1D: Azimuth)
    DOAResult computeSpectralMUSIC(const Eigen::MatrixXcf& Rxx, size_t num_snapshots);

    // Compute 2D Spectral MUSIC across Azimuth and Elevation for URA
    DOAResult computeSpectralMUSIC2D(const Eigen::MatrixXcf& Rxx, size_t num_snapshots);

    // Compute Root-MUSIC polynomial roots directly for ULA (Uniform Linear Array)
    DOAResult computeRootMUSIC(const Eigen::MatrixXcf& Rxx, size_t num_snapshots);

    // Combined DOA estimation: URA -> 2D Spectral, ULA -> Root-MUSIC, UCA -> 1D Spectral
    DOAResult computeDOA(const Eigen::MatrixXcf& Rxx, size_t num_snapshots);

    // Calibrate array phase/gain using Soon et al. (1994) subspace pilot method
    std::vector<std::complex<float>> calibrateFromPilot(const Eigen::MatrixXcf& Rxx, float pilot_angle_deg) const;

    Eigen::VectorXcf getSteeringVectorURA(float azimuth_rad, float elevation_rad) const;

private:
    Eigen::VectorXcf getSteeringVectorULA(float theta_rad) const;
    Eigen::VectorXcf getSteeringVectorUCA(float theta_rad) const;
    Eigen::VectorXcf getSteeringVector(float theta_rad) const;

    void getRootsPolynomial(const Eigen::MatrixXcf& Pi_N, Eigen::VectorXcf& out_roots) const;
    std::vector<float> findSpectralPeaks(const std::vector<float>& angles, const std::vector<float>& spectrum, size_t max_peaks) const;

    DOAConfig config_;
};

} // namespace doa
} // namespace rf_tools
