#include "channelizer.h"
#include <algorithm>
#include <cmath>

namespace kraken_doa::dsp {

    void Channelizer::configure(const ChannelizerConfig& c) {
        cfg = c;
        const double twoPi = 2.0 * M_PI;
        bypass = (c.bandwidthHz <= 0.0 || c.bandwidthHz >= c.sampleRate);
        const double phi = twoPi * c.offsetHz / c.sampleRate;

        if (bypass) {
            decim = 1;
            gr.assign(1, 1.0f);
            gi.assign(1, 0.0f);
            return;
        }

        decim = std::max(1, (int)std::floor(c.sampleRate / (2.0 * c.bandwidthHz)));

        const double trans = c.bandwidthHz / 2.0;
        int taps = (int)std::ceil(5.5 * c.sampleRate / trans);
        taps |= 1; // odd, symmetric around an integer sample
        const int centre = taps / 2;
        const double fcNorm = (c.bandwidthHz / 2.0) / c.sampleRate;

        std::vector<double> h(taps);
        double sum = 0.0;
        for (int k = 0; k < taps; k++) {
            const double n = k - centre;
            const double sinc = (n == 0) ? 2.0 * fcNorm : std::sin(twoPi * fcNorm * n) / (M_PI * n);
            const double w = 0.42 - 0.5 * std::cos(twoPi * k / (taps - 1)) + 0.08 * std::cos(2.0 * twoPi * k / (taps - 1));
            h[k] = sinc * w;
            sum += h[k];
        }

        // y[n] = e^{-j phi n D} sum_k g[k] x[nD + k - centre], g[k] = h[k] e^{-j phi (k - centre)}
        gr.resize(taps);
        gi.resize(taps);
        for (int k = 0; k < taps; k++) {
            const double a = -phi * (k - centre);
            gr[k] = (float)(h[k] / sum * std::cos(a));
            gi[k] = (float)(h[k] / sum * std::sin(a));
        }
    }

    bool Channelizer::process(const std::complex<float>* data, int channels, size_t N) {
        if (!data || channels < 1 || N == 0 || gr.empty()) { return false; }

        const int taps = (int)gr.size();
        const long centre = taps / 2;
        const size_t D = (size_t)decim;
        outLen = N / D;
        if (outLen == 0) { return false; }
        out.resize(outLen * (size_t)channels);

        const double phi = 2.0 * M_PI * cfg.offsetHz / cfg.sampleRate;
        const float* tr = gr.data();
        const float* ti = gi.data();
        double acc = 0.0;

        for (int ch = 0; ch < channels; ch++) {
            const std::complex<float>* x = data + (size_t)ch * N;
            std::complex<float>* y = out.data() + (size_t)ch * outLen;
            const float* xf = reinterpret_cast<const float*>(x);

            for (size_t n = 0; n < outLen; n++) {
                const long start = (long)(n * D) - centre;
                const int k0 = (int)std::max(0L, -start);
                const int k1 = (int)std::min((long)taps, (long)N - start);
                float sr = 0.0f, si = 0.0f;
                const float* p = xf + 2 * (start + k0);
                for (int k = k0; k < k1; k++) {
                    const float xr = p[0], xi = p[1];
                    sr += tr[k] * xr - ti[k] * xi;
                    si += tr[k] * xi + ti[k] * xr;
                    p += 2;
                }
                // Output rotation, phase reduced in double to keep precision at large indices
                const double a = -std::fmod(phi * (double)(n * D), 2.0 * M_PI);
                const float cr = (float)std::cos(a), ci = (float)std::sin(a);
                const std::complex<float> v(sr * cr - si * ci, sr * ci + si * cr);
                y[n] = v;
                acc += (double)std::norm(v);
            }
        }

        const double mean = acc / (double)(outLen * (size_t)channels);
        power = (float)(10.0 * std::log10(mean + 1e-20));
        return true;
    }

}
