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
    size_t getNumChannels() const;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Automatic inter-channel phase & amplitude offset estimation from coherent snapshots
    // Cross-correlation against channel 0. Net correction matches gr-doa's phase_correct, but the stored
    // phase has the opposite sign and the JSON file format differs.
    // Offline/test helper: the live module does not call it (the panel uses MUSICEngine::calibrateFromPilot)
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
    std::vector<std::complex<float>> getComplexWeights() const; // Copy, taken under the lock

    // Save/load calibration profile to/from JSON
    bool saveToFile(const std::string& filePath) const;
    bool loadFromFile(const std::string& filePath);

    std::string getProfileName() const;

private:
    void resetLocked(); // resetToIdentity body; caller holds mutex_ (or is the constructor)

    mutable std::mutex mutex_;
    size_t num_channels_ = 4;
    bool enabled_ = false;
    std::string profile_name_ = "Default Unity";

    // Channel relative phase offset: Delta_phi_k = phi_k - phi_0 = -arg(x_0 * conj(x_k))
    // Complex correction weight to equalize channel k to channel 0:
    // g_k = (1 / |A_k|) * exp(-j * Delta_phi_k) = (1 / |A_k|) * exp(+j * arg(x_0 * conj(x_k)))
    // Such that x_k_cal = x_k * g_k has phase phi_0 and amplitude 1.0 relative to ref channel
    std::vector<std::complex<float>> complex_weights_;
    std::vector<float> measured_phase_rad_;
    std::vector<float> measured_amplitude_;
};

} // namespace doa
} // namespace rf_tools
