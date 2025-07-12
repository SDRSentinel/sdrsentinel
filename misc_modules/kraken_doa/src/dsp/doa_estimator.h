#pragma once
#include <complex>
#include <cstddef>
#include <vector>
#include <Eigen/Dense>

namespace kraken_doa::dsp {

    enum class DoaAlgorithm {
        Bartlett,
        Capon,
        Music
    };

    // A ULA spectrum depends on sin(phi) only (phi = theta + arrayOffset), so phi and
    // 180 - phi are indistinguishable. The scan can be limited to one side of the array axis
    // (the y axis, elements at y = -m d):
    //   Forward:  cos(phi) >= 0, phi in [-90, 90), i.e. the +x side (broadside at theta = 0)
    //   Backward: cos(phi) <  0, phi in [90, 270), the -x side (broadside at theta = 180)
    //   Both:     full 0..359 scan, ULA results show the mirror image
    // Ignored for UCA use (keep Both).
    enum class ScanHalf {
        Both,
        Forward,
        Backward
    };

    struct DoaConfig {
        DoaAlgorithm algorithm = DoaAlgorithm::Music;
        int numSources = 1;          // MUSIC only, 1 <= K < M
        double stepDeg = 1.0;
        double arrayOffsetDeg = 0.0; // added to theta in the steering vector
        double compassOffsetDeg = 0.0;
        ScanHalf half = ScanHalf::Both;
        double diagonalLoading = 1e-3; // Capon, times trace(R)/M
    };

    struct DoaResult {
        std::vector<double> thetaDeg;   // scanned angles
        std::vector<float> spectrumDb;  // normalized to 0 dB at the peak, same size as thetaDeg
        double peakThetaDeg = 0.0;
        double bearingDeg = 0.0;        // (360 - peakTheta + compassOffset) mod 360
        double peakQualityDb = 0.0;     // peak minus median of the spectrum
        std::vector<double> eigenvalues; // of R, descending
    };

    // (360 - theta + compassOffset) mod 360, in [0, 360)
    double bearingFromTheta(double thetaDeg, double compassOffsetDeg);

    class DoaEstimator {
    public:
        // Element positions in wavelengths (see ArrayGeometry::positions)
        bool configure(const std::vector<double>& x, const std::vector<double>& y, const DoaConfig& cfg);

        // data: channel-major, channel k at data + k * N
        bool process(const std::complex<float>* data, size_t N, DoaResult& res);

        // Entry point for an externally accumulated R = X X^H / N
        bool processCovariance(const Eigen::MatrixXcd& R, DoaResult& res);

        static Eigen::MatrixXcd covariance(const std::complex<float>* data, int channels, size_t N);

        int elements() const { return (int)xs.size(); }

    private:
        std::vector<double> xs, ys;
        DoaConfig cfg;
        std::vector<double> theta;
        Eigen::MatrixXcd steering; // M x number of angles
    };

}
