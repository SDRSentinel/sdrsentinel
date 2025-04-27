#include "music_engine.h"
#include <cmath>
#include <limits>
#include <algorithm>

namespace rf_tools {
namespace doa {

static constexpr float PI = 3.14159265358979323846f;
static constexpr float DEG_TO_RAD = PI / 180.0f;
static constexpr float RAD_TO_DEG = 180.0f / PI;

MUSICEngine::MUSICEngine() {
    config_ = DOAConfig{};
}

void MUSICEngine::setConfig(const DOAConfig& config) {
    config_ = config;
}

size_t MUSICEngine::estimateModelOrderMDL(const Eigen::VectorXf& eigenvalues_desc, size_t num_snapshots) const {
    const size_t M = eigenvalues_desc.size();
    if (M <= 1) return 1;

    float min_mdl = std::numeric_limits<float>::max();
    size_t best_k = 1;
    const float N = static_cast<float>(std::max<size_t>(num_snapshots, 1));

    for (size_t k = 0; k < M - 1; ++k) {
        const size_t count = M - k;
        float sum_log = 0.0f;
        float sum_val = 0.0f;

        for (size_t i = k; i < M; ++i) {
            float val = std::max(eigenvalues_desc(i), 1e-12f);
            sum_log += std::log(val);
            sum_val += val;
        }

        float geom_mean = std::exp(sum_log / count);
        float arith_mean = sum_val / count;
        float L = geom_mean / std::max(arith_mean, 1e-12f);

        float penalty = 0.5f * static_cast<float>(k * (2 * M - k)) * std::log(N);
        float mdl = -N * static_cast<float>(count) * std::log(std::clamp(L, 1e-12f, 1.0f)) + penalty;

        if (mdl < min_mdl) {
            min_mdl = mdl;
            best_k = k;
        }
    }

    return std::clamp<size_t>(best_k, 1, M - 1);
}

size_t MUSICEngine::estimateModelOrderAIC(const Eigen::VectorXf& eigenvalues_desc, size_t num_snapshots) const {
    const size_t M = eigenvalues_desc.size();
    if (M <= 1) return 1;

    float min_aic = std::numeric_limits<float>::max();
    size_t best_k = 1;
    const float N = static_cast<float>(std::max<size_t>(num_snapshots, 1));

    for (size_t k = 0; k < M - 1; ++k) {
        const size_t count = M - k;
        float sum_log = 0.0f;
        float sum_val = 0.0f;

        for (size_t i = k; i < M; ++i) {
            float val = std::max(eigenvalues_desc(i), 1e-12f);
            sum_log += std::log(val);
            sum_val += val;
        }

        float geom_mean = std::exp(sum_log / count);
        float arith_mean = sum_val / count;
        float L = geom_mean / std::max(arith_mean, 1e-12f);

        float penalty = 2.0f * static_cast<float>(k * (2 * M - k));
        float aic = -2.0f * N * static_cast<float>(count) * std::log(std::clamp(L, 1e-12f, 1.0f)) + penalty;

        if (aic < min_aic) {
            min_aic = aic;
            best_k = k;
        }
    }

    return std::clamp<size_t>(best_k, 1, M - 1);
}

Eigen::VectorXcf MUSICEngine::getSteeringVectorULA(float theta_rad) const {
    const size_t M = config_.num_elements;
    Eigen::VectorXcf a(M);
    // Centered ULA array locations
    for (size_t m = 0; m < M; ++m) {
        float loc = config_.element_spacing * 0.5f * (static_cast<float>(M) - 1.0f - 2.0f * static_cast<float>(m));
        float phase = -2.0f * PI * std::cos(theta_rad) * loc;
        a(m) = std::complex<float>(std::cos(phase), std::sin(phase));
    }
    return a;
}

Eigen::VectorXcf MUSICEngine::getSteeringVectorUCA(float theta_rad) const {
    const size_t M = config_.num_elements;
    Eigen::VectorXcf a(M);
    // Radius of circular array given element arc spacing d: r = d / (2 * sin(pi / M))
    float radius_lambda = config_.element_spacing / (2.0f * std::sin(PI / static_cast<float>(M)));
    for (size_t m = 0; m < M; ++m) {
        float phi_m = 2.0f * PI * static_cast<float>(m) / static_cast<float>(M);
        float phase = -2.0f * PI * radius_lambda * std::cos(theta_rad - phi_m);
        a(m) = std::complex<float>(std::cos(phase), std::sin(phase));
    }
    return a;
}

Eigen::VectorXcf MUSICEngine::getSteeringVector(float theta_rad) const {
    if (config_.geometry == ArrayGeometry::UCA) {
        return getSteeringVectorUCA(theta_rad);
    }
    return getSteeringVectorULA(theta_rad);
}

void MUSICEngine::getRootsPolynomial(const Eigen::MatrixXcf& Pi_N, Eigen::VectorXcf& out_roots) const {
    const size_t M = Pi_N.rows();
    const size_t poly_len = 2 * M - 1;
    Eigen::VectorXcf u = Eigen::VectorXcf::Zero(poly_len);

    // Diagonal summation of Pi_N to build Laurent polynomial coefficients
    for (int l = -(static_cast<int>(M) - 1); l < 0; ++l) {
        std::complex<float> sum = 0.0f;
        for (size_t i = 0; i < M; ++i) {
            int j = static_cast<int>(i) - l;
            if (j >= 0 && j < static_cast<int>(M)) {
                sum += Pi_N(i, j);
            }
        }
        u(l + M - 1) = sum;
        u(M - 1 - l) = std::conj(sum);
    }

    // Main diagonal
    std::complex<float> main_sum = 0.0f;
    for (size_t i = 0; i < M; ++i) {
        main_sum += Pi_N(i, i);
    }
    u(M - 1) = main_sum;

    // Normalize by highest order coefficient u(2*M - 2)
    std::complex<float> leading = u(2 * M - 2);
    if (std::norm(leading) < 1e-12f) {
        out_roots = Eigen::VectorXcf::Zero(0);
        return;
    }
    u = (-1.0f / leading) * u;

    // Build Frobenius companion matrix of size (2M - 2) x (2M - 2)
    const size_t comp_dim = 2 * M - 2;
    Eigen::MatrixXcf companion_mat = Eigen::MatrixXcf::Zero(comp_dim, comp_dim);

    // Set first sub-diagonal to 1
    for (size_t i = 1; i < comp_dim; ++i) {
        companion_mat(i, i - 1) = 1.0f;
    }

    // Set last column to normalized polynomial coefficients
    for (size_t i = 0; i < comp_dim; ++i) {
        companion_mat(i, comp_dim - 1) = u(i);
    }

    // Solve eigenvalues of companion matrix to get polynomial roots
    Eigen::ComplexEigenSolver<Eigen::MatrixXcf> ces(companion_mat);
    out_roots = ces.eigenvalues();
}

std::vector<float> MUSICEngine::findSpectralPeaks(const std::vector<float>& angles, const std::vector<float>& spectrum, size_t max_peaks) const {
    if (angles.size() != spectrum.size() || spectrum.size() < 3 || max_peaks == 0) {
        return {};
    }

    struct PeakCandidate {
        float angle;
        float power;
    };
    std::vector<PeakCandidate> candidates;

    for (size_t i = 1; i < spectrum.size() - 1; ++i) {
        if (spectrum[i] > spectrum[i - 1] && spectrum[i] > spectrum[i + 1]) {
            // Parabolic interpolation around peak
            float y0 = spectrum[i - 1];
            float y1 = spectrum[i];
            float y2 = spectrum[i + 1];
            float denom = 2.0f * (2.0f * y1 - y0 - y2);
            float delta = 0.0f;
            if (std::abs(denom) > 1e-6f) {
                delta = (y2 - y0) / denom;
            }
            float peak_angle = angles[i] + delta * (angles[i] - angles[i - 1]);
            float peak_val = y1 + 0.5f * delta * (y2 - y0);
            candidates.push_back({peak_angle, peak_val});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const PeakCandidate& a, const PeakCandidate& b) {
        return a.power > b.power;
    });

    std::vector<float> result;
    for (size_t i = 0; i < std::min(candidates.size(), max_peaks); ++i) {
        result.push_back(candidates[i].angle);
    }
    std::sort(result.begin(), result.end());
    return result;
}

DOAResult MUSICEngine::computeSpectralMUSIC(const Eigen::MatrixXcf& Rxx, size_t num_snapshots) {
    DOAResult res;
    const size_t M = Rxx.rows();
    if (M < 2) return res;

    // 1. Eigenvalue decomposition
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXcf> solver(Rxx);
    Eigen::VectorXf eigvals = solver.eigenvalues().reverse(); // Descending order
    Eigen::MatrixXcf eigvecs = solver.eigenvectors().rowwise().reverse();

    // 2. Model order selection
    size_t D = config_.auto_model_order ? estimateModelOrderMDL(eigvals, num_snapshots) : config_.expected_sources;
    D = std::clamp<size_t>(D, 1, M - 1);
    res.model_order = D;

    // 3. Noise subspace projection matrix Pi_N = U_N * U_N^H
    Eigen::MatrixXcf U_N = eigvecs.rightCols(M - D);
    Eigen::MatrixXcf Pi_N = U_N * U_N.adjoint();

    // 4. Scan spatial spectrum
    float start_deg = (config_.geometry == ArrayGeometry::ULA) ? 0.0f : 0.0f;
    float end_deg = (config_.geometry == ArrayGeometry::ULA) ? 180.0f : 360.0f;
    float step_deg = std::max(config_.scan_resolution_deg, 0.1f);
    size_t num_steps = static_cast<size_t>((end_deg - start_deg) / step_deg) + 1;

    res.spectrum_angles_deg.resize(num_steps);
    res.pseudo_spectrum_db.resize(num_steps);

    float max_spectrum = 1e-12f;
    std::vector<float> linear_spectrum(num_steps);

    for (size_t i = 0; i < num_steps; ++i) {
        float angle_deg = start_deg + static_cast<float>(i) * step_deg;
        res.spectrum_angles_deg[i] = angle_deg;

        Eigen::VectorXcf a = getSteeringVector(angle_deg * DEG_TO_RAD);
        std::complex<float> denom = a.adjoint() * Pi_N * a;
        float power = 1.0f / std::max(denom.real(), 1e-12f);
        linear_spectrum[i] = power;
        if (power > max_spectrum) {
            max_spectrum = power;
        }
    }

    // Convert to dB normalized to 0 dB peak
    for (size_t i = 0; i < num_steps; ++i) {
        res.pseudo_spectrum_db[i] = 10.0f * std::log10(std::max(linear_spectrum[i] / max_spectrum, 1e-5f));
    }

    // 5. Extract peak angles
    res.estimated_angles_deg = findSpectralPeaks(res.spectrum_angles_deg, res.pseudo_spectrum_db, D);
    return res;
}

DOAResult MUSICEngine::computeRootMUSIC(const Eigen::MatrixXcf& Rxx, size_t num_snapshots) {
    if (config_.geometry != ArrayGeometry::ULA) {
        return computeSpectralMUSIC(Rxx, num_snapshots);
    }

    DOAResult res;
    const size_t M = Rxx.rows();
    if (M < 2) return res;

    // 1. Eigenvalue decomposition
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXcf> solver(Rxx);
    Eigen::VectorXf eigvals = solver.eigenvalues().reverse();
    Eigen::MatrixXcf eigvecs = solver.eigenvectors().rowwise().reverse();

    // 2. Model order selection
    size_t D = config_.auto_model_order ? estimateModelOrderMDL(eigvals, num_snapshots) : config_.expected_sources;
    D = std::clamp<size_t>(D, 1, M - 1);
    res.model_order = D;

    // 3. Noise subspace projection matrix Pi_N
    Eigen::MatrixXcf U_N = eigvecs.rightCols(M - D);
    Eigen::MatrixXcf Pi_N = U_N * U_N.adjoint();

    // 4. Solve polynomial roots
    Eigen::VectorXcf roots;
    getRootsPolynomial(Pi_N, roots);

    struct RootCandidate {
        std::complex<float> z;
        float dist_to_unit_circle;
    };
    std::vector<RootCandidate> inside_roots;

    for (int i = 0; i < roots.size(); ++i) {
        float r = std::abs(roots(i));
        // Roots strictly inside or near unit circle
        if (r < 1.0f && r >= 0.5f) {
            inside_roots.push_back({roots(i), 1.0f - r});
        }
    }

    std::sort(inside_roots.begin(), inside_roots.end(), [](const RootCandidate& a, const RootCandidate& b) {
        return a.dist_to_unit_circle < b.dist_to_unit_circle;
    });

    std::vector<float> angles;
    for (size_t i = 0; i < std::min(inside_roots.size(), D); ++i) {
        float phase = std::arg(inside_roots[i].z);
        float cos_theta = -phase / (2.0f * PI * config_.element_spacing);
        if (std::abs(cos_theta) <= 1.0f) {
            float angle_deg = std::acos(cos_theta) * RAD_TO_DEG;
            angles.push_back(angle_deg);
        }
    }

    std::sort(angles.begin(), angles.end());
    res.estimated_angles_deg = angles;

    // Also populate pseudo-spectrum for visualization
    DOAResult spec_res = computeSpectralMUSIC(Rxx, num_snapshots);
    res.spectrum_angles_deg = spec_res.spectrum_angles_deg;
    res.pseudo_spectrum_db = spec_res.pseudo_spectrum_db;
    return res;
}

DOAResult MUSICEngine::computeDOA(const Eigen::MatrixXcf& Rxx, size_t num_snapshots) {
    if (config_.geometry == ArrayGeometry::ULA) {
        return computeRootMUSIC(Rxx, num_snapshots);
    }
    return computeSpectralMUSIC(Rxx, num_snapshots);
}

std::vector<std::complex<float>> MUSICEngine::calibrateFromPilot(const Eigen::MatrixXcf& Rxx, float pilot_angle_deg) const {
    const size_t M = Rxx.rows();
    if (M < 2) return {};

    // Subspace method for estimating sensor gains and phases (Soon et al. 1994)
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXcf> solver(Rxx);
    Eigen::VectorXcf u_s = solver.eigenvectors().col(M - 1); // Principal eigenvector

    Eigen::VectorXcf a_pilot = getSteeringVector(pilot_angle_deg * DEG_TO_RAD);

    // Diag matrices
    Eigen::MatrixXcf V = a_pilot.asDiagonal();
    Eigen::MatrixXcf V_conj = a_pilot.conjugate().asDiagonal();

    Eigen::MatrixXcf U_sq = u_s * u_s.adjoint();
    Eigen::MatrixXcf W = V_conj * U_sq * V;

    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXcf> solver_w(W);
    Eigen::VectorXcf cal_vec = solver_w.eigenvectors().col(M - 1); // Eigenvector of unity

    // Normalize reference channel (channel 0) to 1.0 + j0.0
    if (std::norm(cal_vec(0)) > 1e-12f) {
        cal_vec /= cal_vec(0);
    }

    std::vector<std::complex<float>> result(M);
    for (size_t i = 0; i < M; ++i) {
        result[i] = cal_vec(i);
    }
    return result;
}

} // namespace doa
} // namespace rf_tools
