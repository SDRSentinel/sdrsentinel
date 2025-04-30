#pragma once
#include "pdw.h"
#include "pulse_detector.h"
#include "../doa/music_engine.h"
#include <fftw3.h>
#include <vector>
#include <complex>

namespace rf_tools {
    class PulseExtractor {
    public:
        PulseExtractor();
        ~PulseExtractor();

        void init(double sampleRate, double centerFrequency = 0.0);
        void setSampleRate(double sampleRate);
        void setCenterFrequency(double centerFrequency);
        void reset();

        // Standard extraction
        PDW extract(const RawPulse& rawPulse);

        // Multichannel intra-pulse extraction with DOA estimation
        PDW extract(const RawPulse& rawPulse, const std::complex<float>* multichannel_data, size_t num_channels, doa::MUSICEngine* doa_engine);

    private:
        double _sampleRate = 1.0e6;
        double _centerFrequency = 0.0;
        uint64_t _pulseIndex = 0;
        double _lastToa = -1.0;

        // FFT workspace for frequency estimation
        void prepareFftBuffer(size_t requiredSize);
        size_t _fftSize = 0;
        fftwf_complex* _fftIn = nullptr;
        fftwf_complex* _fftOut = nullptr;
        fftwf_plan _fftwPlan = nullptr;
    };
}
