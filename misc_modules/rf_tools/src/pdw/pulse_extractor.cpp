#include "pulse_extractor.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace rf_tools {
    PulseExtractor::PulseExtractor() {}

    PulseExtractor::~PulseExtractor() {
        if (_fftwPlan) {
            fftwf_destroy_plan(_fftwPlan);
            _fftwPlan = nullptr;
        }
        if (_fftIn) {
            fftwf_free(_fftIn);
            _fftIn = nullptr;
        }
        if (_fftOut) {
            fftwf_free(_fftOut);
            _fftOut = nullptr;
        }
    }

    void PulseExtractor::init(double sampleRate, double centerFrequency) {
        _sampleRate = sampleRate;
        _centerFrequency = centerFrequency;
        reset();
    }

    void PulseExtractor::setSampleRate(double sampleRate) {
        _sampleRate = sampleRate;
    }

    void PulseExtractor::setCenterFrequency(double centerFrequency) {
        _centerFrequency = centerFrequency;
    }

    void PulseExtractor::reset() {
        _pulseIndex = 0;
        _lastToa = -1.0;
    }

    void PulseExtractor::prepareFftBuffer(size_t requiredSize) {
        size_t n = 128;
        while (n < requiredSize) {
            n <<= 1;
        }

        if (n == _fftSize && _fftwPlan) {
            return;
        }

        if (_fftwPlan) {
            fftwf_destroy_plan(_fftwPlan);
            _fftwPlan = nullptr;
        }
        if (_fftIn) {
            fftwf_free(_fftIn);
        }
        if (_fftOut) {
            fftwf_free(_fftOut);
        }

        _fftSize = n;
        _fftIn = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
        _fftOut = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
        _fftwPlan = fftwf_plan_dft_1d(_fftSize, _fftIn, _fftOut, FFTW_FORWARD, FFTW_ESTIMATE);
    }

    PDW PulseExtractor::extract(const RawPulse& rawPulse) {
        PDW pdw;
        pdw.pulseIndex = ++_pulseIndex;
        pdw.sampleIndex = rawPulse.startSample;
        pdw.pulseWidthSamples = static_cast<uint32_t>(rawPulse.samples.size());
        pdw.pulseWidth = pdw.pulseWidthSamples / _sampleRate;
        pdw.toa = rawPulse.startSample / _sampleRate;

        // PRI calculation
        if (_lastToa >= 0.0) {
            pdw.pri = pdw.toa - _lastToa;
        } else {
            pdw.pri = 0.0;
        }
        _lastToa = pdw.toa;

        if (rawPulse.samples.empty()) {
            return pdw;
        }

        // 1. Peak power, average power, initial phase
        float maxPwrLinear = 0.0f;
        double sumPwrLinear = 0.0;
        for (const auto& s : rawPulse.samples) {
            float pwr = s.re * s.re + s.im * s.im;
            if (pwr > maxPwrLinear) {
                maxPwrLinear = pwr;
            }
            sumPwrLinear += pwr;
        }

        float avgPwrLinear = static_cast<float>(sumPwrLinear / rawPulse.samples.size());
        pdw.peakPower = 10.0f * std::log10(std::max(maxPwrLinear, 1e-15f));
        pdw.averagePower = 10.0f * std::log10(std::max(avgPwrLinear, 1e-15f));
        pdw.phase = std::atan2(rawPulse.samples[0].im, rawPulse.samples[0].re);

        // 2. Noise floor calculation
        if (!rawPulse.noiseSamples.empty()) {
            double sumNoise = 0.0;
            for (const auto& ns : rawPulse.noiseSamples) {
                sumNoise += (ns.re * ns.re + ns.im * ns.im);
            }
            float avgNoise = static_cast<float>(sumNoise / rawPulse.noiseSamples.size());
            pdw.noiseFloor = 10.0f * std::log10(std::max(avgNoise, 1e-15f));
        } else {
            pdw.noiseFloor = -100.0f;
        }
        pdw.snr = pdw.averagePower - pdw.noiseFloor;

        // 3. Frequency offset calculation using zero-padded FFT
        size_t paddedSize = rawPulse.samples.size() + 128;
        prepareFftBuffer(paddedSize);

        // Zero out buffer and copy pulse samples to center
        std::memset(_fftIn, 0, _fftSize * sizeof(fftwf_complex));
        size_t startOffset = (_fftSize - rawPulse.samples.size()) / 2;
        for (size_t i = 0; i < rawPulse.samples.size(); ++i) {
            _fftIn[startOffset + i][0] = rawPulse.samples[i].re;
            _fftIn[startOffset + i][1] = rawPulse.samples[i].im;
        }

        fftwf_execute(_fftwPlan);

        // Find peak bin in shifted FFT
        int peakBin = 0;
        float peakMag = -1.0f;
        std::vector<float> magnitudes(_fftSize);

        for (size_t i = 0; i < _fftSize; ++i) {
            size_t srcIdx = (i + _fftSize / 2) % _fftSize;
            float mag = _fftOut[srcIdx][0] * _fftOut[srcIdx][0] + _fftOut[srcIdx][1] * _fftOut[srcIdx][1];
            magnitudes[i] = mag;
            if (mag > peakMag) {
                peakMag = mag;
                peakBin = static_cast<int>(i);
            }
        }

        // Sub-bin quadratic peak interpolation
        double delta = 0.0;
        if (peakBin > 0 && peakBin < static_cast<int>(_fftSize) - 1) {
            float y0 = magnitudes[peakBin - 1];
            float y1 = magnitudes[peakBin];
            float y2 = magnitudes[peakBin + 1];
            float denom = 2.0f * (y0 - 2.0f * y1 + y2);
            if (std::abs(denom) > 1e-12f) {
                delta = (y0 - y2) / denom;
            }
        }

        double binFreqResolution = _sampleRate / static_cast<double>(_fftSize);
        pdw.frequencyOffset = (static_cast<double>(peakBin) - static_cast<double>(_fftSize) / 2.0 + delta) * binFreqResolution;
        pdw.absoluteFrequency = _centerFrequency + pdw.frequencyOffset;

        return pdw;
    }
}
