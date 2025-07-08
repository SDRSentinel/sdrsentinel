#pragma once
#include <cstdint>
#include <limits>

namespace rf_tools {
    struct PDW {
        uint64_t pulseIndex = 0;
        uint64_t sampleIndex = 0;
        double toa = 0.0;                 // Time of Arrival (seconds)
        double pulseWidth = 0.0;          // Pulse Width (seconds)
        uint32_t pulseWidthSamples = 0;   // Pulse Width (sample count)
        double frequencyOffset = 0.0;     // Frequency offset relative to center (Hz)
        double absoluteFrequency = 0.0;    // Absolute carrier frequency (Hz)
        float peakPower = -120.0f;        // Peak power (dBFS)
        float averagePower = -120.0f;     // Average power during pulse (dBFS)
        float snr = 0.0f;                 // Signal-to-noise ratio (dB)
        float noiseFloor = -120.0f;       // Estimated noise floor (dBFS)
        float phase = 0.0f;               // Initial phase (radians)
        double pri = 0.0;                 // Pulse Repetition Interval (seconds, 0 if first pulse)
        // Kept for the binary export layout (format v2); direction finding is in the kraken_doa module,
        // so rf_tools never sets these: doaAzimuth stays NaN and hasDoa stays false
        float doaAzimuth = std::numeric_limits<float>::quiet_NaN();
        bool hasDoa = false;

        // Hardware power calibration (gr-pdw standard: dBm)
        float peakPowerDbm = -999.0f;
        float averagePowerDbm = -999.0f;
        float noiseFloorDbm = -999.0f;
        bool isCalibratedDbm = false;
        float refLevelDbm = 0.0f;

        // gr-pdw timing compatibility
        int64_t courseToa = 0;            // Whole seconds
        uint64_t fineToa = 0;             // Clock cycles / sample index within second
    };
}
