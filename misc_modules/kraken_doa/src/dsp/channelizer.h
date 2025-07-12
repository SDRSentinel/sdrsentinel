#pragma once
#include <complex>
#include <cstddef>
#include <vector>

namespace kraken_doa::dsp {

    struct ChannelizerConfig {
        double sampleRate = 2.4e6;  // Hz, from the frame header
        double offsetHz = 0.0;      // Signal of interest relative to the frame center frequency
        double bandwidthHz = 12.5e3;
    };

    // Frequency shift by -offsetHz, windowed-sinc low-pass (Blackman, -6 dB at bandwidth/2,
    // transition width bandwidth/2) and integer decimation, identical for all channels so
    // inter-channel phase is preserved. Only the kept output samples are computed.
    // Edges are zero padded, output length is floor(N / decimation). The group delay is
    // compensated (centered taps), the same for every channel.
    // Bypass (bandwidth >= sampleRate or <= 0): no filtering or decimation, only the shift.
    class Channelizer {
    public:
        void configure(const ChannelizerConfig& cfg);

        // data: channel-major, channel k at data + k * N. Returns false on invalid arguments.
        bool process(const std::complex<float>* data, int channels, size_t N);

        // Channel-major, channel k at output() + k * outputLength()
        const std::complex<float>* output() const { return out.data(); }
        size_t outputLength() const { return outLen; }
        int decimation() const { return decim; }
        double outputRate() const { return cfg.sampleRate / decim; }
        int tapCount() const { return (int)gr.size(); }
        bool bypassed() const { return bypass; }

        // Mean |x|^2 over all channels and output samples in dBFS (1.0 = full scale)
        float powerDbfs() const { return power; }

    private:
        ChannelizerConfig cfg;
        bool bypass = true;
        int decim = 1;
        std::vector<float> gr, gi; // taps with the frequency shift folded in
        std::vector<std::complex<float>> out;
        size_t outLen = 0;
        float power = -200.0f;
    };

}
