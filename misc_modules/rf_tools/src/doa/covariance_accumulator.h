#pragma once
#include "types.h"
#include <mutex>
#include <vector>
#include <complex>

namespace rf_tools {
namespace doa {

class CovarianceAccumulator {
public:
    CovarianceAccumulator();
    ~CovarianceAccumulator() = default;

    void init(size_t num_elements, size_t snapshot_size = 256, float forgetting_factor = 0.0f, bool enable_fbss = false);

    // Set per-channel complex calibration factors (e.g. gain and phase offset)
    void setCalibration(const std::vector<std::complex<float>>& cal_factors);
    void clearCalibration();

    // Feed a single snapshot of M complex samples
    void feedSnapshot(const std::complex<float>* snapshot_data, size_t num_elements);

    // Feed multiple snapshots (interleaved: [s0_ch0, s0_ch1, ..., s1_ch0, ...])
    void feedSnapshotsInterleaved(const std::complex<float>* data, size_t num_elements, size_t num_snapshots);

    // Feed multiple snapshots (channel planar: [ch0_s0..ch0_sN, ch1_s0..ch1_sN, ...])
    void feedSnapshotsPlanar(const std::complex<float>* const* channel_data, size_t num_elements, size_t num_snapshots);

    // Retrieve normalized covariance matrix if ready. Returns true when a new matrix is produced.
    bool getNormalizedCovariance(Eigen::MatrixXcf& out_Rxx);

    void reset();

    size_t getSnapshotCount() const { return snapshot_count_; }
    size_t getNumElements() const { return num_elements_; }

private:
    void applyFBSS(Eigen::MatrixXcf& mat) const;

    size_t num_elements_{4};
    size_t snapshot_size_{256};
    float alpha_{0.0f}; // Forgetting factor
    bool use_recursive_{false};
    bool enable_fbss_{false};

    size_t snapshot_count_{0};
    uint64_t total_snapshots_{0};
    bool matrix_ready_{false};

    Eigen::MatrixXcf Rxx_;
    Eigen::MatrixXcf latest_ready_Rxx_;
    Eigen::VectorXcf cal_factors_;
    bool has_calibration_{false};

    mutable std::mutex mutex_;
};

} // namespace doa
} // namespace rf_tools
