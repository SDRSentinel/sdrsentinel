#pragma once
#include <dsp/types.h>
#include <vector>
#include <complex>
#include <string>

namespace rf_tools {

enum class MOPType {
    UNMODULATED_CW,
    LFM_CHIRP_UP,
    LFM_CHIRP_DOWN,
    PHASE_BARKER,
    FREQUENCY_HOPPING,
    UNKNOWN
};

struct STFTSpectrogram {
    size_t numTimeBins = 0;
    size_t numFreqBins = 0;
    std::vector<float> dataDb; // Flattened [time x freq] in dB
    float minDb = -60.0f;
    float maxDb = 0.0f;
    float freqStartHz = 0.0f;
    float freqEndHz = 0.0f;
};

struct MOPResult {
    MOPType type = MOPType::UNMODULATED_CW;
    float chirpBandwidthHz = 0.0f;
    float chirpSlopeMhzPerUs = 0.0f; // MHz/us (standard radar convention)
    float chirpLinearityR2 = 0.0f;
    int barkerLength = 0;
    float barkerCorrelation = 0.0f;
    float meanFreqOffsetHz = 0.0f;

    std::vector<float> envelope;
    std::vector<float> instFreqHz;
    std::vector<float> instPhaseRad;
    std::vector<float> iSamples;
    std::vector<float> qSamples;
    STFTSpectrogram spectrogram;
};

class MOPAnalyzer {
public:
    MOPAnalyzer() = default;
    ~MOPAnalyzer() = default;

    static MOPResult analyze(const std::vector<dsp::complex_t>& samples, double sampleRate, double centerFreqHz);
    static const char* getMOPTypeName(MOPType type);

private:
    static void computeSTFT(const std::vector<dsp::complex_t>& samples,
                            double sampleRate,
                            size_t fftSize,
                            size_t hopSize,
                            STFTSpectrogram& outSpectrogram);

    static float correlateBarker(const std::vector<float>& phaseDiff, int& outCodeLength);
};

} // namespace rf_tools
