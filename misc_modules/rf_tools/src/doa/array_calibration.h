#pragma once
#include <vector>
#include <complex>
#include <string>
#include <mutex>
#include <Eigen/Dense>

namespace rf_tools {
namespace doa {

class ArrayCalibration {
public:
    ArrayCalibration(size_t num_channels = 4);
    ~ArrayCalibration() = default;

    void setNumChannels(size_t num_channels);
    size_t getNumChannels() const { return num_channels_; }

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    // Automatic inter-channel phase & amplitude offset estimation from coherent snapshots
    // Inspired by Ettus gr-doa twinrx_phase_offset_est
    bool estimateFromSnapshots(const std::complex<float>* interleaved_snapshots, size_t num_channels, size_t num_snapshots);

    // Apply phase and amplitude correction in-place to interleaved snapshots [K snapshots x M channels]
    void applyCorrection(std::complex<float>* interleaved_snapshots, size_t num_channels, size_t num_snapshots) const;

    // Apply correction to an already formed covariance matrix: R_cal = C * R * C^H
    void applyCorrectionCovariance(Eigen::MatrixXcf& Rxx) const;

    // Reset calibration to unity (0 dB gain, 0 deg phase offset)
    void resetToIdentity();

    // Set individual element correction manually
    void setElementCorrection(size_t channel, float gain_linear, float phase_rad);

    // Getters for display / debugging
    std::vector<float> getPhaseOffsetsDeg() const;
    std::vector<float> getRelativeGainsDb() const;
    const std::vector<std::complex<float>>& getComplexWeights() const { return complex_weights_; }

    // Save/load calibration profile to/from JSON
    bool saveToFile(const std::string& filePath) const;
    bool loadFromFile(const std::string& filePath);

    std::string getProfileName() const { return profile_name_; }

private:
    mutable std::mutex mutex_;
    size_t num_channels_ = 4;
    bool enabled_ = false;
    std::string profile_name_ = "Default Unity";

    // Complex correction coefficients: g_k = (1/|A_k|) * exp(-j * phi_k)
    std::vector<std::complex<float>> complex_weights_;
    std::vector<float> measured_phase_rad_;
    std::vector<float> measured_amplitude_;
};

} // namespace doa
} // namespace rf_tools
