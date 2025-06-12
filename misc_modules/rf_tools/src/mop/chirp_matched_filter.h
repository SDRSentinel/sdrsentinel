#pragma once
#include <vector>
#include <complex>
#include <fftw3.h>
#include <mutex>
#include <string>

namespace rf_tools {
namespace mop {

struct ChirpCompressionResult {
    bool detected = false;
    double chirpRateHzPerSec = 0.0;
    double chirpBandwidthHz = 0.0;
    double uncompressedPulseWidthSec = 0.0;
    double compressedPulseWidthSec = 0.0;
    float compressionRatio = 1.0f;
    float peakCompressedSnrGainDb = 0.0f;
    float pslrDb = 0.0f;                     // Peak-to-Sidelobe Ratio in dB
    std::vector<float> compressedEnvelope;    // Normalized compressed pulse magnitude
};

class ChirpMatchedFilter {
public:
    ChirpMatchedFilter();
    ~ChirpMatchedFilter();

    void init(double sampleRate);
    void setSampleRate(double sampleRate) { _sampleRate = sampleRate; }

    // Synthesize reference linear frequency modulation (LFM) chirp replica
    // Inspired by GPU_SDR chirp_gen
    std::vector<std::complex<float>> generateReferenceChirp(double durationSec, double startFreqHz, double chirpRateHzPerSec) const;

    // Direct De-Chirp mixing: out[n] = in[n] * conj(ref[n])
    // Inspired directly by GPU_SDR chirp_demodulator
    void dechirpMix(const std::complex<float>* input, size_t count, double startFreqHz, double chirpRateHzPerSec, std::complex<float>* output) const;

    enum class WindowType {
        RECTANGULAR = 0,
        HAMMING,
        HANN
    };

    // Matched filter pulse compression via FFT fast convolution
    ChirpCompressionResult compressPulse(const std::vector<std::complex<float>>& pulseSamples, double estimatedChirpRate, WindowType window = WindowType::RECTANGULAR);

private:
    void prepareFft(size_t n);

    double _sampleRate = 2.0e6;
    size_t _fftSize = 0;
    fftwf_complex* _fftIn = nullptr;
    fftwf_complex* _fftRef = nullptr;
    fftwf_complex* _fftOut = nullptr;
    fftwf_plan _planFwdIn = nullptr;
    fftwf_plan _planFwdRef = nullptr;
    fftwf_plan _planInv = nullptr;
    mutable std::mutex _mutex;
};

} // namespace mop
} // namespace rf_tools
