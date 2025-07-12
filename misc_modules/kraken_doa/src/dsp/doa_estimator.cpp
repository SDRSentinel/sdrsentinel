#include "doa_estimator.h"
#include <algorithm>
#include <cmath>

namespace kraken_doa::dsp {

    double bearingFromTheta(double thetaDeg, double compassOffsetDeg) {
        double b = std::fmod(360.0 - thetaDeg + compassOffsetDeg, 360.0);
        if (b < 0.0) { b += 360.0; }
        return b;
    }

    bool DoaEstimator::configure(const std::vector<double>& x, const std::vector<double>& y, const DoaConfig& c) {
        const int M = (int)x.size();
        if (M < 2 || y.size() != x.size() || c.stepDeg <= 0.0) { return false; }
        if (c.algorithm == DoaAlgorithm::Music && (c.numSources < 1 || c.numSources >= M)) { return false; }
        xs = x;
        ys = y;
        cfg = c;

        theta.clear();
        for (double t = 0.0; t < 360.0 - 1e-9; t += cfg.stepDeg) {
            const double phi = std::fmod(t + cfg.arrayOffsetDeg, 360.0) * M_PI / 180.0;
            const double cp = std::cos(phi);
            if (cfg.half == ScanHalf::Forward && cp < -1e-12) { continue; }
            if (cfg.half == ScanHalf::Backward && cp >= -1e-12) { continue; }
            theta.push_back(t);
        }
        if (theta.empty()) { return false; }

        steering.resize(M, theta.size());
        for (size_t i = 0; i < theta.size(); i++) {
            const double phi = (theta[i] + cfg.arrayOffsetDeg) * M_PI / 180.0;
            const double c0 = std::cos(phi), s0 = std::sin(phi);
            for (int m = 0; m < M; m++) {
                const double a = 2.0 * M_PI * (xs[m] * c0 + ys[m] * s0);
                steering(m, i) = std::complex<double>(std::cos(a), std::sin(a));
            }
        }
        return true;
    }

    Eigen::MatrixXcd DoaEstimator::covariance(const std::complex<float>* data, int M, size_t N) {
        Eigen::MatrixXcd R = Eigen::MatrixXcd::Zero(M, M);
        for (int i = 0; i < M; i++) {
            const std::complex<float>* xi = data + (size_t)i * N;
            for (int j = i; j < M; j++) {
                const std::complex<float>* xj = data + (size_t)j * N;
                double sr = 0.0, si = 0.0;
                for (size_t n = 0; n < N; n++) {
                    // xi * conj(xj)
                    const double ar = xi[n].real(), ai = xi[n].imag();
                    const double br = xj[n].real(), bi = xj[n].imag();
                    sr += ar * br + ai * bi;
                    si += ai * br - ar * bi;
                }
                R(i, j) = std::complex<double>(sr, si) / (double)N;
                R(j, i) = std::conj(R(i, j));
            }
        }
        return R;
    }

    bool DoaEstimator::process(const std::complex<float>* data, size_t N, DoaResult& res) {
        if (!data || N == 0 || steering.cols() == 0) { return false; }
        return processCovariance(covariance(data, elements(), N), res);
    }

    bool DoaEstimator::processCovariance(const Eigen::MatrixXcd& R, DoaResult& res) {
        const int M = elements();
        if (steering.cols() == 0 || R.rows() != M || R.cols() != M) { return false; }

        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXcd> es(R);
        if (es.info() != Eigen::Success) { return false; }
        res.eigenvalues.resize(M);
        for (int i = 0; i < M; i++) { res.eigenvalues[i] = es.eigenvalues()(M - 1 - i); }

        const size_t nA = theta.size();
        Eigen::ArrayXd p(nA);

        if (cfg.algorithm == DoaAlgorithm::Bartlett) {
            Eigen::MatrixXcd RA = R * steering;
            p = (steering.conjugate().cwiseProduct(RA)).colwise().sum().real().array();
        }
        else if (cfg.algorithm == DoaAlgorithm::Capon) {
            const double tr = R.trace().real();
            Eigen::MatrixXcd Rl = R + Eigen::MatrixXcd::Identity(M, M) * (cfg.diagonalLoading * tr / M + 1e-30);
            Eigen::MatrixXcd X = Rl.ldlt().solve(steering);
            p = 1.0 / (steering.conjugate().cwiseProduct(X)).colwise().sum().real().array().max(1e-300);
        }
        else {
            const int K = cfg.numSources;
            Eigen::MatrixXcd En = es.eigenvectors().leftCols(M - K); // ascending eigenvalues
            Eigen::MatrixXcd proj = En.adjoint() * steering;
            p = 1.0 / proj.colwise().squaredNorm().array().max(1e-300);
        }

        Eigen::Index peak;
        const double pmax = p.maxCoeff(&peak);
        res.thetaDeg = theta;
        res.spectrumDb.resize(nA);
        std::vector<float> sorted(nA);
        for (size_t i = 0; i < nA; i++) {
            res.spectrumDb[i] = (float)(10.0 * std::log10(std::max(p(i) / pmax, 1e-30)));
            sorted[i] = res.spectrumDb[i];
        }
        std::nth_element(sorted.begin(), sorted.begin() + nA / 2, sorted.end());
        res.peakThetaDeg = theta[peak];
        res.bearingDeg = bearingFromTheta(res.peakThetaDeg, cfg.compassOffsetDeg);
        res.peakQualityDb = -(double)sorted[nA / 2];
        return true;
    }

}
