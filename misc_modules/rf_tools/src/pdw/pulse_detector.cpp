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

    void PulseDetector::setAutoThreshold(bool enable) {
        _auto = enable;
        updateLinearThreshold();
    }

    void PulseDetector::setNoiseMargin(float marginDb) {
        _marginDb = marginDb;
    }

    float PulseDetector::getNoiseFloorDb() const {
        return 10.0f * std::log10(static_cast<float>(_noisePower) + 1.0e-20f);
    }

    float PulseDetector::getEffectiveThresholdDb() const {
        return 20.0f * std::log10(_linearThreshold + 1.0e-10f);
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
        if (!_auto) {
            _linearThreshold = std::pow(10.0f, _thresholdDb / 20.0f);
            _endThreshold = _linearThreshold * 0.708f; // 3 dB hysteresis
        }
    }

    void PulseDetector::updateAutoThreshold() {
        if (!_auto || !_noiseInit) return;
        _linearThreshold = std::max(1.0e-6f, std::sqrt(static_cast<float>(_noisePower)) * std::pow(10.0f, _marginDb / 20.0f));
        _endThreshold = _linearThreshold * 0.708f;
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
        _noiseInit = false;
        _noisePower = 0.0;
        for (size_t i = 0; i < PRE_NOISE_WINDOW; ++i) {
            _recentNoise[i] = { 0.0f, 0.0f };
        }
    }

    void PulseDetector::process(const dsp::complex_t* data, int count) {
        if (!data || count <= 0) return;

        updateAutoThreshold();

        for (int i = 0; i < count; ++i) {
            dsp::complex_t s = data[i];
            float power = s.re * s.re + s.im * s.im;
            float mag = std::sqrt(power);

            if (_state == SEARCHING) {
                // Keep pre-pulse ring buffer for noise floor estimation
                _recentNoise[_noiseIdx] = s;
                _noiseIdx = (_noiseIdx + 1) % PRE_NOISE_WINDOW;

                // Slow running average of the power outside pulses
                if (!_noiseInit) {
                    _noisePower = power;
                    _noiseInit = true;
                }
                else {
                    _noisePower += 1.0e-3 * (power - _noisePower);
                }

                // Let the noise floor settle before detecting
                if (_auto && _totalSamples + i < WARMUP_SAMPLES) continue;

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

                bool pulseEnded = (mag < _endThreshold);
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
