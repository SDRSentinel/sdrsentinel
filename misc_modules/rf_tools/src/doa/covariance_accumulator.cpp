#include "covariance_accumulator.h"
#include <algorithm>

namespace rf_tools {
namespace doa {

CovarianceAccumulator::CovarianceAccumulator() {
    init(4, 256, 0.0f, false);
}

void CovarianceAccumulator::init(size_t num_elements, size_t snapshot_size, float forgetting_factor, bool enable_fbss) {
    std::lock_guard<std::mutex> lock(mutex_);
    num_elements_ = std::max<size_t>(num_elements, 2);
    snapshot_size_ = std::max<size_t>(snapshot_size, 16);
    alpha_ = std::clamp(forgetting_factor, 0.0f, 0.999f);
    use_recursive_ = (alpha_ > 0.0f);
    enable_fbss_ = enable_fbss;

    Rxx_ = Eigen::MatrixXcf::Zero(num_elements_, num_elements_);
    latest_ready_Rxx_ = Eigen::MatrixXcf::Zero(num_elements_, num_elements_);
    cal_factors_ = Eigen::VectorXcf::Ones(num_elements_);
    has_calibration_ = false;

    snapshot_count_ = 0;
    total_snapshots_ = 0;
    matrix_ready_ = false;
    has_latest_ = false;
}

void CovarianceAccumulator::setCalibration(const std::vector<std::complex<float>>& cal_factors) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cal_factors.size() != num_elements_) {
        return;
    }
    cal_factors_.resize(num_elements_);
    for (size_t i = 0; i < num_elements_; ++i) {
        cal_factors_(i) = cal_factors[i];
    }
    has_calibration_ = true;
}

void CovarianceAccumulator::clearCalibration() {
    std::lock_guard<std::mutex> lock(mutex_);
    cal_factors_ = Eigen::VectorXcf::Ones(num_elements_);
    has_calibration_ = false;
}

void CovarianceAccumulator::feedSnapshot(const std::complex<float>* snapshot_data, size_t num_elements) {
    if (num_elements != num_elements_ || !snapshot_data) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    Eigen::VectorXcf x(num_elements_);
    for (size_t i = 0; i < num_elements_; ++i) {
        x(i) = snapshot_data[i];
    }

    // Apply calibration if available: x_cal[i] = x[i] / cal[i]
    if (has_calibration_) {
        for (size_t i = 0; i < num_elements_; ++i) {
            if (std::norm(cal_factors_(i)) > 1e-12f) {
                x(i) /= cal_factors_(i);
            }
        }
    }

    if (use_recursive_) {
        // Recursive EMA update: R[n] = alpha * R[n-1] + (1 - alpha) * (x * x^H)
        Rxx_ = alpha_ * Rxx_ + (1.0f - alpha_) * (x * x.adjoint());
        snapshot_count_++;
        total_snapshots_++;

        if (snapshot_count_ >= snapshot_size_) {
            latest_ready_Rxx_ = Rxx_;
            if (enable_fbss_) {
                applyFBSS(latest_ready_Rxx_);
            }
            matrix_ready_ = true;
            has_latest_ = true;
        }
    } else {
        // Block Rank-1 update on upper triangle
        Rxx_.selfadjointView<Eigen::Upper>().rankUpdate(x);
        snapshot_count_++;
        total_snapshots_++;

        if (snapshot_count_ >= snapshot_size_) {
            // Normalize by number of accumulated snapshots
            Eigen::MatrixXcf full_Rxx = Rxx_.selfadjointView<Eigen::Upper>();
            full_Rxx /= static_cast<float>(snapshot_count_);

            if (enable_fbss_) {
                applyFBSS(full_Rxx);
            }

            latest_ready_Rxx_ = full_Rxx;
            matrix_ready_ = true;
            has_latest_ = true;

            // Reset for next block
            Rxx_.setZero();
            snapshot_count_ = 0;
        }
    }
}

void CovarianceAccumulator::feedSnapshotsInterleaved(const std::complex<float>* data, size_t num_elements, size_t num_snapshots) {
    if (!data || num_elements != num_elements_) {
        return;
    }
    for (size_t s = 0; s < num_snapshots; ++s) {
        feedSnapshot(data + s * num_elements, num_elements);
    }
}

void CovarianceAccumulator::feedSnapshotsPlanar(const std::complex<float>* const* channel_data, size_t num_elements, size_t num_snapshots) {
    if (!channel_data || num_elements != num_elements_) {
        return;
    }
    std::vector<std::complex<float>> snapshot(num_elements);
    for (size_t s = 0; s < num_snapshots; ++s) {
        for (size_t c = 0; c < num_elements; ++c) {
            snapshot[c] = channel_data[c][s];
        }
        feedSnapshot(snapshot.data(), num_elements);
    }
}

bool CovarianceAccumulator::getNormalizedCovariance(Eigen::MatrixXcf& out_Rxx) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!matrix_ready_) {
        return false;
    }
    out_Rxx = latest_ready_Rxx_;
    matrix_ready_ = false; // Consumed
    return true;
}

bool CovarianceAccumulator::getLatestCovariance(Eigen::MatrixXcf& out_Rxx) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_latest_) {
        return false;
    }
    out_Rxx = latest_ready_Rxx_;
    return true;
}

void CovarianceAccumulator::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    Rxx_.setZero();
    latest_ready_Rxx_.setZero();
    snapshot_count_ = 0;
    matrix_ready_ = false;
    has_latest_ = false;
}

void CovarianceAccumulator::applyFBSS(Eigen::MatrixXcf& mat) const {
    // Forward-Backward Spatial Smoothing:
    // R_FBSS = 0.5 * (R + J * R^* * J)
    // J is the exchange matrix (anti-diagonal identity)
    const size_t M = mat.rows();
    Eigen::MatrixXcf J = Eigen::MatrixXcf::Zero(M, M);
    for (size_t i = 0; i < M; ++i) {
        J(i, M - 1 - i) = 1.0f;
    }

    Eigen::MatrixXcf R_conj = mat.conjugate();
    Eigen::MatrixXcf R_back = J * R_conj * J;
    mat = 0.5f * (mat + R_back);
}

} // namespace doa
} // namespace rf_tools
