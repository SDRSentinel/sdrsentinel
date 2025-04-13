#pragma once
#include <dsp/types.h>
#include <vector>
#include <functional>
#include <cmath>
#include <cstdint>

namespace rf_tools {
    struct RawPulse {
        uint64_t startSample = 0;
        uint64_t endSample = 0;
        std::vector<dsp::complex_t> samples;
        std::vector<dsp::complex_t> noiseSamples;
    };

    class PulseDetector {
    public:
        using PulseCallback = std::function<void(const RawPulse&)>;

        PulseDetector();
        ~PulseDetector();

        void init(double sampleRate, float thresholdDb = -40.0f, double minPw = 0.5e-6, double maxPw = 1000.0e-6);
        void setSampleRate(double sampleRate);
        void setThreshold(float thresholdDb);
        void setAutoThreshold(bool enable);
        void setNoiseMargin(float marginDb);
        void setPulseWidthLimits(double minPw, double maxPw);
        void setPulseCallback(PulseCallback callback);

        void reset();
        void process(const dsp::complex_t* data, int count);

        float getThreshold() const { return _thresholdDb; }
        bool getAutoThreshold() const { return _auto; }
        float getNoiseMargin() const { return _marginDb; }
        float getNoiseFloorDb() const;
        float getEffectiveThresholdDb() const;
        double getMinPulseWidth() const { return _minPw; }
        double getMaxPulseWidth() const { return _maxPw; }
        uint64_t getTotalPulses() const { return _totalPulses; }

    private:
        enum State {
            SEARCHING,
            IN_PULSE
        };

        void updateLinearThreshold();
        void updateAutoThreshold();
        void updateSampleLimits();

        double _sampleRate = 1.0e6;
        float _thresholdDb = -40.0f;
        float _linearThreshold = 0.01f;
        float _endThreshold = 0.0071f;
        bool _auto = true;
        float _marginDb = 12.0f;
        double _noisePower = 0.0;
        bool _noiseInit = false;
        static constexpr uint64_t WARMUP_SAMPLES = 4096;
        double _minPw = 0.5e-6;
        double _maxPw = 1000.0e-6;

        uint32_t _minSamples = 1;
        uint32_t _maxSamples = 100000;

        State _state = SEARCHING;
        uint64_t _totalSamples = 0;
        uint64_t _pulseStartSample = 0;
        uint64_t _totalPulses = 0;

        std::vector<dsp::complex_t> _pulseBuffer;
        std::vector<dsp::complex_t> _noiseBuffer;
        static constexpr size_t PRE_NOISE_WINDOW = 16;
        dsp::complex_t _recentNoise[PRE_NOISE_WINDOW];
        size_t _noiseIdx = 0;

        PulseCallback _callback;
    };
}
