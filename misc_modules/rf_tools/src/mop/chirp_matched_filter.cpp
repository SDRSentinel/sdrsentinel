#include "chirp_matched_filter.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <utils/flog.h>

namespace rf_tools {
namespace mop {

static constexpr double TWO_PI = 6.28318530717958647692;

ChirpMatchedFilter::ChirpMatchedFilter() {}

ChirpMatchedFilter::~ChirpMatchedFilter() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_planFwdIn) fftwf_destroy_plan(_planFwdIn);
    if (_planFwdRef) fftwf_destroy_plan(_planFwdRef);
    if (_planInv) fftwf_destroy_plan(_planInv);
    if (_fftIn) fftwf_free(_fftIn);
    if (_fftRef) fftwf_free(_fftRef);
    if (_fftOut) fftwf_free(_fftOut);
}

void ChirpMatchedFilter::init(double sampleRate) {
    _sampleRate = sampleRate;
}

void ChirpMatchedFilter::prepareFft(size_t requiredSize) {
    size_t n = 256;
    while (n < requiredSize) {
        n <<= 1;
    }

    if (n == _fftSize && _planFwdIn) return;

    if (_planFwdIn) fftwf_destroy_plan(_planFwdIn);
    if (_planFwdRef) fftwf_destroy_plan(_planFwdRef);
    if (_planInv) fftwf_destroy_plan(_planInv);
    if (_fftIn) fftwf_free(_fftIn);
    if (_fftRef) fftwf_free(_fftRef);
    if (_fftOut) fftwf_free(_fftOut);

    _fftSize = n;
    _fftIn = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
    _fftRef = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
    _fftOut = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));

    _planFwdIn = fftwf_plan_dft_1d(_fftSize, _fftIn, _fftIn, FFTW_FORWARD, FFTW_ESTIMATE);
    _planFwdRef = fftwf_plan_dft_1d(_fftSize, _fftRef, _fftRef, FFTW_FORWARD, FFTW_ESTIMATE);
    _planInv = fftwf_plan_dft_1d(_fftSize, _fftOut, _fftOut, FFTW_BACKWARD, FFTW_ESTIMATE);
}

std::vector<std::complex<float>> ChirpMatchedFilter::generateReferenceChirp(double durationSec, double startFreqHz, double chirpRateHzPerSec) const {
    size_t count = static_cast<size_t>(std::max(1.0, std::round(durationSec * _sampleRate)));
    std::vector<std::complex<float>> ref(count);

    double dt = 1.0 / _sampleRate;
    for (size_t n = 0; n < count; ++n) {
        double t = n * dt;
        double phase = TWO_PI * (startFreqHz * t + 0.5 * chirpRateHzPerSec * t * t);
        ref[n] = std::complex<float>(
            static_cast<float>(std::cos(phase)),
            static_cast<float>(std::sin(phase))
        );
    }
    return ref;
}

void ChirpMatchedFilter::dechirpMix(const std::complex<float>* input, size_t count, double startFreqHz, double chirpRateHzPerSec, std::complex<float>* output) const {
    if (!input || !output || count == 0) return;

    double dt = 1.0 / _sampleRate;
    for (size_t n = 0; n < count; ++n) {
        double t = n * dt;
        double phase = TWO_PI * (startFreqHz * t + 0.5 * chirpRateHzPerSec * t * t);
        // conj(chirp): cos(phase) - j * sin(phase)
        std::complex<float> refConj(
            static_cast<float>(std::cos(phase)),
            static_cast<float>(-std::sin(phase))
        );
        output[n] = input[n] * refConj;
    }
}

ChirpCompressionResult ChirpMatchedFilter::compressPulse(const std::vector<std::complex<float>>& pulseSamples, double estimatedChirpRate) {
    ChirpCompressionResult res;
    if (pulseSamples.size() < 16 || std::abs(estimatedChirpRate) < 1.0e6) {
        return res;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    size_t N = pulseSamples.size();
    double durationSec = N / _sampleRate;
    double chirpBandwidth = std::abs(estimatedChirpRate * durationSec);

    // Prepare FFT size for linear convolution (N + N - 1)
    size_t convSize = 2 * N;
    prepareFft(convSize);

    // 1. Generate reference chirp centered at 0 offset
    double startFreq = -0.5 * estimatedChirpRate * durationSec;
    auto ref = generateReferenceChirp(durationSec, startFreq, estimatedChirpRate);

    // 2. Zero-pad and load pulse into _fftIn
    std::memset(_fftIn, 0, _fftSize * sizeof(fftwf_complex));
    for (size_t i = 0; i < N; ++i) {
        _fftIn[i][0] = pulseSamples[i].real();
        _fftIn[i][1] = pulseSamples[i].imag();
    }

    // 3. Zero-pad and load time-reversed conjugated reference into _fftRef (matched filter impulse response h[n] = ref*[N-1-n])
    std::memset(_fftRef, 0, _fftSize * sizeof(fftwf_complex));
    for (size_t i = 0; i < N; ++i) {
        _fftRef[i][0] = ref[N - 1 - i].real();
        _fftRef[i][1] = -ref[N - 1 - i].imag();
    }

    // 4. Forward FFTs
    fftwf_execute(_planFwdIn);
    fftwf_execute(_planFwdRef);

    // 5. Frequency-domain multiplication
    for (size_t i = 0; i < _fftSize; ++i) {
        float rA = _fftIn[i][0], iA = _fftIn[i][1];
        float rB = _fftRef[i][0], iB = _fftRef[i][1];
        _fftOut[i][0] = (rA * rB - iA * iB);
        _fftOut[i][1] = (rA * iB + iA * rB);
    }

    // 6. Inverse FFT
    fftwf_execute(_planInv);

    // 7. Extract compressed envelope around convolution peak
    size_t validLen = 2 * N - 1;
    std::vector<float> envelope(validLen);
    float maxMag = 0.0f;
    size_t peakIdx = 0;

    for (size_t i = 0; i < validLen; ++i) {
        float mag = std::sqrt(_fftOut[i][0] * _fftOut[i][0] + _fftOut[i][1] * _fftOut[i][1]) / static_cast<float>(_fftSize);
        envelope[i] = mag;
        if (mag > maxMag) {
            maxMag = mag;
            peakIdx = i;
        }
    }

    if (maxMag <= 1e-9f) {
        return res;
    }

    // Normalize envelope
    for (float& v : envelope) {
        v /= maxMag;
    }

    // Find 3 dB width around peak
    size_t leftIdx = peakIdx;
    while (leftIdx > 0 && envelope[leftIdx] >= 0.707f) {
        --leftIdx;
    }
    size_t rightIdx = peakIdx;
    while (rightIdx < validLen - 1 && envelope[rightIdx] >= 0.707f) {
        ++rightIdx;
    }

    double compressedWidthSamples = std::max(1.0, static_cast<double>(rightIdx - leftIdx));
    double compressedWidthSec = compressedWidthSamples / _sampleRate;

    // Estimate peak sidelobe level (outside mainlobe)
    float maxSidelobe = 0.0f;
    for (size_t i = 0; i < validLen; ++i) {
        if (i < leftIdx || i > rightIdx) {
            if (envelope[i] > maxSidelobe) {
                maxSidelobe = envelope[i];
            }
        }
    }

    float pslrDb = 20.0f * std::log10(std::max(maxSidelobe, 1e-4f));

    // Populate result
    res.detected = true;
    res.chirpRateHzPerSec = estimatedChirpRate;
    res.chirpBandwidthHz = chirpBandwidth;
    res.uncompressedPulseWidthSec = durationSec;
    res.compressedPulseWidthSec = compressedWidthSec;
    res.compressionRatio = static_cast<float>(durationSec / compressedWidthSec);
    res.peakCompressedSnrGainDb = 10.0f * std::log10(std::max(res.compressionRatio, 1.0f));
    res.pslrDb = pslrDb;
    res.compressedEnvelope = std::move(envelope);

    return res;
}

} // namespace mop
} // namespace rf_tools
