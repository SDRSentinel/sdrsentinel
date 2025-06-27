#include "array_calibration.h"
#include <cmath>
#include <fstream>
#include <json.hpp>
#include <utils/flog.h>

namespace rf_tools {
namespace doa {

ArrayCalibration::ArrayCalibration(size_t num_channels)
    : num_channels_(num_channels) {
    resetLocked();
}

void ArrayCalibration::setNumChannels(size_t num_channels) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (num_channels == num_channels_) return;
    num_channels_ = num_channels;
    resetLocked();
}

void ArrayCalibration::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = enabled;
}

bool ArrayCalibration::isEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_;
}

size_t ArrayCalibration::getNumChannels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return num_channels_;
}

std::vector<std::complex<float>> ArrayCalibration::getComplexWeights() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return complex_weights_;
}

std::string ArrayCalibration::getProfileName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return profile_name_;
}

void ArrayCalibration::resetToIdentity() {
    std::lock_guard<std::mutex> lock(mutex_);
    resetLocked();
}

void ArrayCalibration::resetLocked() {
    complex_weights_.assign(num_channels_, std::complex<float>(1.0f, 0.0f));
    measured_phase_rad_.assign(num_channels_, 0.0f);
    measured_amplitude_.assign(num_channels_, 1.0f);
    profile_name_ = "Identity (Uncalibrated)";
}

void ArrayCalibration::setElementCorrection(size_t channel, float gain_linear, float phase_rad) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (channel >= num_channels_) return;

    measured_amplitude_[channel] = gain_linear;
    measured_phase_rad_[channel] = phase_rad;

    float inv_gain = (std::abs(gain_linear) > 1e-6f) ? (1.0f / gain_linear) : 1.0f;
    complex_weights_[channel] = std::polar(inv_gain, -phase_rad);
}

bool ArrayCalibration::estimateFromSnapshots(const std::complex<float>* interleaved_snapshots, size_t num_channels, size_t num_snapshots) {
    if (!interleaved_snapshots || num_channels == 0 || num_snapshots == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (num_channels != num_channels_) {
        num_channels_ = num_channels;
        resetLocked();
    }

    // Accumulate cross-correlation with reference channel 0: sum_n (x_0[n] * conj(x_k[n]))
    // and total power of each channel: sum_n |x_k[n]|^2
    std::vector<std::complex<double>> cross_corr(num_channels, { 0.0, 0.0 });
    std::vector<double> powers(num_channels, 0.0);

    for (size_t n = 0; n < num_snapshots; ++n) {
        const std::complex<float>* snap = &interleaved_snapshots[n * num_channels];
        std::complex<double> x0(snap[0].real(), snap[0].imag());

        for (size_t k = 0; k < num_channels; ++k) {
            std::complex<double> xk(snap[k].real(), snap[k].imag());
            cross_corr[k] += x0 * std::conj(xk);
            powers[k] += std::norm(xk);
        }
    }

    double ref_pwr = powers[0] / static_cast<double>(num_snapshots);
    if (ref_pwr < 1e-12) {
        flog::warn("ArrayCalibration: Reference channel power too low for calibration");
        return false;
    }

    for (size_t k = 0; k < num_channels; ++k) {
        double ch_pwr = powers[k] / static_cast<double>(num_snapshots);
        float amp_rel = static_cast<float>(std::sqrt(ch_pwr / ref_pwr));
        if (amp_rel < 1e-6f) amp_rel = 1.0f;

        // Cross-correlation: R_0k = sum_n (x_0[n] * conj(x_k[n]))
        // Argument of R_0k: arg(R_0k) = phi_0 - phi_k
        // Hardware phase offset of channel k relative to reference channel 0:
        // Delta_phi_k = phi_k - phi_0 = -arg(R_0k)
        double phase_offset_rel = -std::arg(cross_corr[k]);

        measured_amplitude_[k] = amp_rel;
        measured_phase_rad_[k] = static_cast<float>(phase_offset_rel);

        // Equalization weight to align channel k to channel 0:
        // x_k_cal = x_k * g_k = x_k * (1 / amp_rel) * exp(-j * Delta_phi_k)
        //                     = x_k * (1 / amp_rel) * exp(+j * arg(R_0k))
        // -> phase(x_k_cal) = phi_k - Delta_phi_k = phi_k - (phi_k - phi_0) = phi_0
        float inv_amp = 1.0f / amp_rel;
        complex_weights_[k] = std::polar(inv_amp, -static_cast<float>(phase_offset_rel));
    }

    enabled_ = true;
    profile_name_ = "Live Measured Pilot Calibration";
    flog::debug("ArrayCalibration: Successfully estimated calibration from {0} snapshots", std::to_string(num_snapshots));
    return true;
}

void ArrayCalibration::applyCorrection(std::complex<float>* interleaved_snapshots, size_t num_channels, size_t num_snapshots) const {
    if (!interleaved_snapshots) return;

    // Copy the weights under the lock so the UI thread can reset/reload them while this runs
    std::vector<std::complex<float>> weights;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_ || num_channels != num_channels_ || complex_weights_.size() != num_channels) return;
        weights = complex_weights_;
    }

    for (size_t n = 0; n < num_snapshots; ++n) {
        std::complex<float>* snap = &interleaved_snapshots[n * num_channels];
        for (size_t k = 0; k < num_channels; ++k) {
            snap[k] *= weights[k];
        }
    }
}

void ArrayCalibration::applyCorrectionCovariance(Eigen::MatrixXcf& Rxx) const {
    std::vector<std::complex<float>> weights;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_ || static_cast<size_t>(Rxx.rows()) != num_channels_ || complex_weights_.size() != num_channels_) return;
        weights = complex_weights_;
    }

    // R_cal = C * R * C^H, where C = diag(weights)
    for (size_t r = 0; r < weights.size(); ++r) {
        for (size_t c = 0; c < weights.size(); ++c) {
            Rxx(r, c) *= (weights[r] * std::conj(weights[c]));
        }
    }
}

std::vector<float> ArrayCalibration::getPhaseOffsetsDeg() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<float> res(num_channels_);
    for (size_t i = 0; i < num_channels_; ++i) {
        float deg = measured_phase_rad_[i] * 180.0f / static_cast<float>(M_PI);
        while (deg > 180.0f) deg -= 360.0f;
        while (deg < -180.0f) deg += 360.0f;
        res[i] = deg;
    }
    return res;
}

std::vector<float> ArrayCalibration::getRelativeGainsDb() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<float> res(num_channels_);
    for (size_t i = 0; i < num_channels_; ++i) {
        res[i] = 20.0f * std::log10(std::max(measured_amplitude_[i], 1e-6f));
    }
    return res;
}

bool ArrayCalibration::saveToFile(const std::string& filePath) const {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        nlohmann::json root;
        root["num_channels"] = num_channels_;
        root["profile_name"] = profile_name_;
        root["enabled"] = enabled_;

        nlohmann::json elements = nlohmann::json::array();
        for (size_t i = 0; i < num_channels_; ++i) {
            elements.push_back({
                { "channel", i },
                { "amplitude_linear", measured_amplitude_[i] },
                { "phase_rad", measured_phase_rad_[i] },
                { "gain_db", 20.0f * std::log10(std::max(measured_amplitude_[i], 1e-6f)) },
                { "phase_deg", measured_phase_rad_[i] * 180.0f / static_cast<float>(M_PI) },
                { "weight_real", complex_weights_[i].real() },
                { "weight_imag", complex_weights_[i].imag() }
            });
        }
        root["elements"] = elements;

        std::ofstream out(filePath);
        if (!out.is_open()) return false;
        out << root.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

bool ArrayCalibration::loadFromFile(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::ifstream in(filePath);
        if (!in.is_open()) return false;

        nlohmann::json root;
        in >> root;

        size_t nch = root.value("num_channels", 4);
        num_channels_ = nch;
        complex_weights_.resize(nch);
        measured_amplitude_.resize(nch);
        measured_phase_rad_.resize(nch);

        profile_name_ = root.value("profile_name", filePath);
        enabled_ = root.value("enabled", true);

        if (root.contains("elements")) {
            for (const auto& el : root["elements"]) {
                size_t ch = el.value("channel", 0);
                if (ch < nch) {
                    measured_amplitude_[ch] = el.value("amplitude_linear", 1.0f);
                    measured_phase_rad_[ch] = el.value("phase_rad", 0.0f);
                    float w_re = el.value("weight_real", 1.0f);
                    float w_im = el.value("weight_imag", 0.0f);
                    complex_weights_[ch] = std::complex<float>(w_re, w_im);
                }
            }
        }

        flog::info("ArrayCalibration: Loaded profile '{}' from {}", profile_name_, filePath);
        return true;
    } catch (const std::exception& e) {
        flog::error("ArrayCalibration: Failed to parse {}: {}", filePath, e.what());
        return false;
    }
}

} // namespace doa
} // namespace rf_tools
