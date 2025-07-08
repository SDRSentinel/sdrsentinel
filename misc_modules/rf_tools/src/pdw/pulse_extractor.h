#pragma once
#include "pdw.h"
#include "pulse_detector.h"
#include <fftw3.h>
#include <vector>
#include <complex>

namespace rf_tools {
    class PowerCalibrationTable;

    class PulseExtractor {
    public:
        PulseExtractor();
        ~PulseExtractor();

        void init(double sampleRate, double centerFrequency = 0.0);
        void setSampleRate(double sampleRate);
        void setCenterFrequency(double centerFrequency);
        void setCalibrationTable(const PowerCalibrationTable* calTable) { _calTable = calTable; }
        void reset();

        PDW extract(const RawPulse& rawPulse);

    private:
        double _sampleRate = 1.0e6;
        double _centerFrequency = 0.0;
        uint64_t _pulseIndex = 0;
        double _lastToa = -1.0;
        const PowerCalibrationTable* _calTable = nullptr;

        // FFT workspace for frequency estimation
        void prepareFftBuffer(size_t requiredSize);
        size_t _fftSize = 0;
        fftwf_complex* _fftIn = nullptr;
        fftwf_complex* _fftOut = nullptr;
        fftwf_plan _fftwPlan = nullptr;
    };
}
