#include "mop_analyzer.h"
#include "chirp_matched_filter.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fftw3.h>

namespace rf_tools {

static constexpr float PI = 3.14159265358979323846f;
static constexpr float TWO_PI = 2.0f * PI;

const char* MOPAnalyzer::getMOPTypeName(MOPType type) {
    switch (type) {
        case MOPType::UNMODULATED_CW:    return "CW (Unmodulated)";
        case MOPType::LFM_CHIRP_UP:      return "LFM Chirp (Up-Ramp)";
        case MOPType::LFM_CHIRP_DOWN:    return "LFM Chirp (Down-Ramp)";
        case MOPType::PHASE_BARKER:      return "Phase-Coded (Barker)";
        case MOPType::FREQUENCY_HOPPING: return "Frequency Hopping";
        default:                         return "Unknown / Complex";
    }
}

float MOPAnalyzer::correlateBarker(const std::vector<float>& phaseSamples, int& outCodeLength) {
    if (phaseSamples.size() < 16) {
        outCodeLength = 0;
        return 0.0f;
    }

    // Known Barker sequences
    static const std::vector<int> b7  = { 1, 1, 1, -1, -1, 1, -1 };
    static const std::vector<int> b11 = { 1, 1, 1, -1, -1, -1, 1, -1, -1, 1, -1 };
    static const std::vector<int> b13 = { 1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1 };

    // Convert phase to BPSK symbols (+1 for [ -pi/2, pi/2 ], -1 otherwise)
    std::vector<float> bpsk(phaseSamples.size());
    for (size_t i = 0; i < phaseSamples.size(); ++i) {
        float p = std::fmod(phaseSamples[i] + PI, TWO_PI) - PI;
        bpsk[i] = (std::abs(p) < (PI * 0.5f)) ? 1.0f : -1.0f;
    }

    float bestCorr = 0.0f;
    int bestLen = 0;

    auto testSequence = [&](const std::vector<int>& code) {
        size_t codeLen = code.size();
        size_t minOversample = 2;
        size_t maxOversample = phaseSamples.size() / codeLen;
        if (maxOversample < minOversample) return;

        for (size_t os = minOversample; os <= std::min(maxOversample, (size_t)32); ++os) {
            std::vector<float> expanded(codeLen * os);
            for (size_t i = 0; i < codeLen; ++i) {
                for (size_t k = 0; k < os; ++k) {
                    expanded[i * os + k] = static_cast<float>(code[i]);
                }
            }

            size_t maxShift = phaseSamples.size() - expanded.size();
            for (size_t shift = 0; shift <= maxShift; shift += (os > 4 ? os / 2 : 1)) {
                float dot = 0.0f;
                float normA = 0.0f;
                float normB = 0.0f;
                for (size_t j = 0; j < expanded.size(); ++j) {
                    dot += bpsk[shift + j] * expanded[j];
                    normA += bpsk[shift + j] * bpsk[shift + j];
                    normB += expanded[j] * expanded[j];
                }
                float corr = (normA > 0.0f && normB > 0.0f) ? std::abs(dot) / std::sqrt(normA * normB) : 0.0f;
                if (corr > bestCorr) {
                    bestCorr = corr;
                    bestLen = static_cast<int>(codeLen);
                }
            }
        }
    };

    testSequence(b7);
    testSequence(b11);
    testSequence(b13);

    outCodeLength = bestLen;
    return bestCorr;
}

void MOPAnalyzer::computeSTFT(const std::vector<dsp::complex_t>& samples,
                             double sampleRate,
                             size_t fftSize,
                             size_t hopSize,
                             STFTSpectrogram& outSpectrogram) {
    if (samples.size() < fftSize) {
        outSpectrogram.numTimeBins = 0;
        outSpectrogram.numFreqBins = 0;
        return;
    }

    size_t numHops = (samples.size() - fftSize) / hopSize + 1;
    outSpectrogram.numTimeBins = numHops;
    outSpectrogram.numFreqBins = fftSize;
    outSpectrogram.dataDb.resize(numHops * fftSize);
    outSpectrogram.freqStartHz = -static_cast<float>(sampleRate * 0.5);
    outSpectrogram.freqEndHz = static_cast<float>(sampleRate * 0.5);

    // Prepare Hanning window
    std::vector<float> window(fftSize);
    for (size_t i = 0; i < fftSize; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(TWO_PI * static_cast<float>(i) / static_cast<float>(fftSize - 1)));
    }

    fftwf_complex* in = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
    fftwf_complex* out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
    fftwf_plan plan = fftwf_plan_dft_1d(static_cast<int>(fftSize), in, out, FFTW_FORWARD, FFTW_ESTIMATE);

    float globalMax = -120.0f;
    float globalMin = 0.0f;

    for (size_t h = 0; h < numHops; ++h) {
        size_t startIdx = h * hopSize;
        for (size_t i = 0; i < fftSize; ++i) {
            in[i][0] = samples[startIdx + i].re * window[i];
            in[i][1] = samples[startIdx + i].im * window[i];
        }

        fftwf_execute(plan);

        // FFT shift and convert to dB
        for (size_t i = 0; i < fftSize; ++i) {
            size_t shiftedIdx = (i + fftSize / 2) % fftSize;
            float magSq = out[shiftedIdx][0] * out[shiftedIdx][0] + out[shiftedIdx][1] * out[shiftedIdx][1];
            float db = 10.0f * std::log10(std::max(magSq, 1e-12f));

            outSpectrogram.dataDb[h * fftSize + i] = db;
            if (db > globalMax) globalMax = db;
            if (db < globalMin) globalMin = db;
        }
    }

    fftwf_destroy_plan(plan);
    fftwf_free(in);
    fftwf_free(out);

    outSpectrogram.maxDb = globalMax;
    outSpectrogram.minDb = std::max(globalMin, globalMax - 50.0f);
}

MOPResult MOPAnalyzer::analyze(const std::vector<dsp::complex_t>& samples, double sampleRate, double centerFreqHz) {
    (void)centerFreqHz;
    MOPResult result;
    size_t N = samples.size();
    if (N < 8) return result;

    result.envelope.resize(N);
    result.iSamples.resize(N);
    result.qSamples.resize(N);
    std::vector<float> rawPhase(N);

    for (size_t i = 0; i < N; ++i) {
        result.iSamples[i] = samples[i].re;
        result.qSamples[i] = samples[i].im;
        result.envelope[i] = std::sqrt(samples[i].re * samples[i].re + samples[i].im * samples[i].im);
        rawPhase[i] = std::atan2(samples[i].im, samples[i].re);
    }

    // Phase unwrapping
    result.instPhaseRad.resize(N);
    result.instPhaseRad[0] = rawPhase[0];
    for (size_t i = 1; i < N; ++i) {
        float diff = rawPhase[i] - rawPhase[i - 1];
        while (diff > PI) diff -= TWO_PI;
        while (diff < -PI) diff += TWO_PI;
        result.instPhaseRad[i] = result.instPhaseRad[i - 1] + diff;
    }

    // Instantaneous frequency via central finite differences
    result.instFreqHz.resize(N);
    float dt = 1.0f / static_cast<float>(sampleRate);
    result.instFreqHz[0] = (result.instPhaseRad[1] - result.instPhaseRad[0]) / (TWO_PI * dt);
    for (size_t i = 1; i < N - 1; ++i) {
        result.instFreqHz[i] = (result.instPhaseRad[i + 1] - result.instPhaseRad[i - 1]) / (2.0f * TWO_PI * dt);
    }
    result.instFreqHz[N - 1] = (result.instPhaseRad[N - 1] - result.instPhaseRad[N - 2]) / (TWO_PI * dt);

    // 3-point moving average smoothing on instFreq
    std::vector<float> smoothFreq = result.instFreqHz;
    for (size_t i = 1; i < N - 1; ++i) {
        smoothFreq[i] = (result.instFreqHz[i - 1] + result.instFreqHz[i] + result.instFreqHz[i + 1]) / 3.0f;
    }

    // Linear regression on central 80% of pulse to detect Chirp / LFM
    size_t startIdx = N * 10 / 100;
    size_t endIdx = N * 90 / 100;
    if (endIdx <= startIdx + 4) {
        startIdx = 0;
        endIdx = N;
    }

    size_t M = endIdx - startIdx;
    double sumT = 0.0, sumF = 0.0, sumTT = 0.0, sumTF = 0.0;
    for (size_t i = startIdx; i < endIdx; ++i) {
        double t = static_cast<double>(i) * dt;
        double f = smoothFreq[i];
        sumT += t;
        sumF += f;
        sumTT += t * t;
        sumTF += t * f;
    }

    double meanT = sumT / M;
    double meanF = sumF / M;
    double varT = sumTT - sumT * meanT;
    double covTF = sumTF - sumT * meanF;

    double slopeHzPerSec = (std::abs(varT) > 1e-18) ? (covTF / varT) : 0.0;
    double interceptHz = meanF - slopeHzPerSec * meanT;
    result.meanFreqOffsetHz = static_cast<float>(meanF);

    // Goodness of fit (R^2)
    double ssTot = 0.0, ssRes = 0.0;
    for (size_t i = startIdx; i < endIdx; ++i) {
        double t = static_cast<double>(i) * dt;
        double f = smoothFreq[i];
        double fFit = interceptHz + slopeHzPerSec * t;
        ssTot += (f - meanF) * (f - meanF);
        ssRes += (f - fFit) * (f - fFit);
    }

    float r2 = (ssTot > 1e-6) ? static_cast<float>(std::clamp(1.0 - (ssRes / ssTot), 0.0, 1.0)) : 0.0f;
    result.chirpLinearityR2 = r2;

    float pulseDurationUs = static_cast<float>(M * dt * 1.0e6);
    float freqSpanHz = static_cast<float>(std::abs(slopeHzPerSec) * (M * dt));
    result.chirpBandwidthHz = freqSpanHz;
    // MHz / us = (Hz/s) * 1e-12
    result.chirpSlopeMhzPerUs = static_cast<float>(slopeHzPerSec * 1.0e-12);

    // Barker code correlation
    int barkerLen = 0;
    float barkerCorr = correlateBarker(rawPhase, barkerLen);
    result.barkerLength = barkerLen;
    result.barkerCorrelation = barkerCorr;

    // Classification decision logic
    float fsThreshold = static_cast<float>(sampleRate * 0.04);
    if (r2 > 0.70f && freqSpanHz > fsThreshold && pulseDurationUs > 0.5f) {
        if (slopeHzPerSec > 0.0) {
            result.type = MOPType::LFM_CHIRP_UP;
        } else {
            result.type = MOPType::LFM_CHIRP_DOWN;
        }
    } else if (barkerCorr > 0.78f && barkerLen > 0) {
        result.type = MOPType::PHASE_BARKER;
    } else {
        result.type = MOPType::UNMODULATED_CW;
    }

    // Chirp matched filter pulse compression
    if (result.type == MOPType::LFM_CHIRP_UP || result.type == MOPType::LFM_CHIRP_DOWN) {
        mop::ChirpMatchedFilter mf;
        mf.init(sampleRate);
        std::vector<std::complex<float>> cplx(N);
        for (size_t i = 0; i < N; ++i) {
            cplx[i] = { samples[i].re, samples[i].im };
        }
        auto compRes = mf.compressPulse(cplx, slopeHzPerSec);
        if (compRes.detected) {
            result.hasChirpCompression = true;
            result.compressionRatio = compRes.compressionRatio;
            result.compressedSnrGainDb = compRes.peakCompressedSnrGainDb;
            result.pslrDb = compRes.pslrDb;
            result.compressedEnvelope = std::move(compRes.compressedEnvelope);
        }
    }

    // Mini STFT Spectrogram
    size_t fftSize = 64;
    if (N < 64) fftSize = 32;
    if (N >= 32) {
        size_t hopSize = std::max((size_t)2, fftSize / 4);
        computeSTFT(samples, sampleRate, fftSize, hopSize, result.spectrogram);
    }

    return result;
}

} // namespace rf_tools
