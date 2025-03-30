#include "pulse_detector.h"
#include <algorithm>

namespace rf_tools {
    PulseDetector::PulseDetector() {
        for (size_t i = 0; i < PRE_NOISE_WINDOW; ++i) {
            _recentNoise[i] = { 0.0f, 0.0f };
        }
    }

    PulseDetector::~PulseDetector() {}

    void PulseDetector::init(double sampleRate, float thresholdDb, double minPw, double maxPw) {
        _sampleRate = sampleRate;
        _thresholdDb = thresholdDb;
        _minPw = minPw;
        _maxPw = maxPw;

        updateLinearThreshold();
        updateSampleLimits();
        reset();
    }

    void PulseDetector::setSampleRate(double sampleRate) {
        _sampleRate = sampleRate;
        updateSampleLimits();
    }

    void PulseDetector::setThreshold(float thresholdDb) {
        _thresholdDb = thresholdDb;
        updateLinearThreshold();
    }

    void PulseDetector::setPulseWidthLimits(double minPw, double maxPw) {
        _minPw = minPw;
        _maxPw = maxPw;
        updateSampleLimits();
    }

    void PulseDetector::setPulseCallback(PulseCallback callback) {
        _callback = callback;
    }

    void PulseDetector::updateLinearThreshold() {
        // thresholdDb is in dBFS (where 1.0 peak amplitude is 0 dBFS)
        _linearThreshold = std::pow(10.0f, _thresholdDb / 20.0f);
    }

    void PulseDetector::updateSampleLimits() {
        if (_sampleRate <= 0.0) return;
        _minSamples = std::max<uint32_t>(1, static_cast<uint32_t>(std::round(_minPw * _sampleRate)));
        _maxSamples = std::max<uint32_t>(_minSamples + 1, static_cast<uint32_t>(std::round(_maxPw * _sampleRate)));
    }

    void PulseDetector::reset() {
        _state = SEARCHING;
        _totalSamples = 0;
        _pulseStartSample = 0;
        _totalPulses = 0;
        _pulseBuffer.clear();
        _noiseBuffer.clear();
        _noiseIdx = 0;
        for (size_t i = 0; i < PRE_NOISE_WINDOW; ++i) {
            _recentNoise[i] = { 0.0f, 0.0f };
        }
    }

    void PulseDetector::process(const dsp::complex_t* data, int count) {
        if (!data || count <= 0) return;

        for (int i = 0; i < count; ++i) {
            dsp::complex_t s = data[i];
            float mag = std::sqrt(s.re * s.re + s.im * s.im);

            if (_state == SEARCHING) {
                // Keep pre-pulse ring buffer for noise floor estimation
                _recentNoise[_noiseIdx] = s;
                _noiseIdx = (_noiseIdx + 1) % PRE_NOISE_WINDOW;

                if (mag >= _linearThreshold) {
                    _state = IN_PULSE;
                    _pulseStartSample = _totalSamples + i;
                    _pulseBuffer.clear();
                    _pulseBuffer.push_back(s);

                    // Capture pre-pulse noise
                    _noiseBuffer.clear();
                    _noiseBuffer.reserve(PRE_NOISE_WINDOW);
                    for (size_t k = 0; k < PRE_NOISE_WINDOW; ++k) {
                        _noiseBuffer.push_back(_recentNoise[(_noiseIdx + k) % PRE_NOISE_WINDOW]);
                    }
                }
            }
            else { // IN_PULSE
                _pulseBuffer.push_back(s);

                bool pulseEnded = (mag < _linearThreshold);
                bool pulseOverflow = (_pulseBuffer.size() > _maxSamples);

                if (pulseEnded || pulseOverflow) {
                    uint32_t pulseLen = static_cast<uint32_t>(_pulseBuffer.size());
                    if (pulseLen >= _minSamples && pulseLen <= _maxSamples) {
                        _totalPulses++;
                        if (_callback) {
                            RawPulse pulse;
                            pulse.startSample = _pulseStartSample;
                            pulse.endSample = _totalSamples + i;
                            pulse.samples = std::move(_pulseBuffer);
                            pulse.noiseSamples = std::move(_noiseBuffer);
                            _callback(pulse);
                        }
                    }

                    _state = SEARCHING;
                    _pulseBuffer.clear();
                    _noiseBuffer.clear();
                }
            }
        }

        _totalSamples += count;
    }
}
