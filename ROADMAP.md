# rf_tools: pulse analysis and direction finding for SDR++

Design notes and roadmap

## Summary

`rf_tools` is an analysis module built into SDR++. It finds short pulses (radar, transponders and similar) inside a selected band, extracts parameters such as time, width, frequency and power for each pulse, and looks at the time between pulses to tell the emitter type apart. The next goal is to estimate the direction of arrival of a signal received on several simultaneous channels. The algorithms come from two GNU Radio projects (gr-pdw and gr-doa), but they are rewritten in C++ inside the SDR++ DSP pipeline, without depending on GNU Radio. This document describes the method, the integration with SDR++, how it will be validated, and the known limitations.

## 1. Purpose and scope

SDR++ is a receiver with wide hardware support. It has no ready-made tool for analyzing pulsed signals. There are GNU Radio projects that do this, but they need a separate installation and a Python runtime. The aim here is to do the same work inside SDR++, with no extra dependencies.

The scope has two parts:

- Pulse analysis (PDW, pulse descriptor word): detection, parameter extraction, PRI analysis.
- Direction finding (DOA): estimating the angle of arrival with MUSIC and Root-MUSIC.

Out of scope: separating several emitters from each other (deinterleaving), a recognition and classification database, and hardware synchronization.

## 2. Background

### 2.1 Pulse parameters

A pulse is usually described by these values: time of arrival (TOA), pulse width (PW), intra-pulse frequency, peak and average power, and SNR. The TOA difference to the previous pulse gives the PRI. gr-pdw [1] extracts these parameters as GNU Radio blocks.

### 2.2 PRI analysis

The PRI pattern of radar pulses helps to tell emitters apart. There are three basic patterns: constant, stagger (cycling between a few fixed values) and jitter (random deviation). The works of Mardia [7] and Milojević and Popović [8] on deinterleaving pulse trains are the main references.

### 2.3 Direction finding

The covariance matrix of simultaneous samples from an antenna array is computed, and an eigenvalue decomposition separates the signal and noise subspaces. MUSIC [5] scans for directions orthogonal to the noise subspace. For linear arrays, Root-MUSIC [6] finds polynomial roots instead of scanning. gr-doa [2] contains both.

## 3. Method

### 3.1 Pulse detection

The input is the complex IQ stream of the selected VFO. The magnitude of each sample is compared with a threshold. The detector has two states: searching and in pulse. The threshold can be given in two ways:

- Manually, in dBFS.
- Automatically: a slow moving average of the power of the samples outside pulses is kept as the noise floor, and the threshold is a given number of dB above it (12 by default).

The end of a pulse uses a threshold 3 dB below the start threshold, so there is hysteresis. This way noise near the threshold does not split a single pulse. Only pulses between the minimum and maximum pulse width are accepted.

### 3.2 Parameter extraction

- PW is the number of samples in the pulse divided by the sample rate.
- TOA is the time of the first sample of the pulse.
- The frequency offset is found by zero-padding the pulse samples and taking an FFT. A parabola is fitted through three points around the peak bin to get sub-bin resolution. The absolute frequency is the VFO center frequency plus the offset.
- Power is given as peak and average inside the pulse, in dBFS. The noise floor is estimated from the 16 samples just before the pulse, and SNR is the difference between average power and the noise floor.
- PRI is the TOA difference between two consecutive pulses.

### 3.3 PRI classification

The last 256 PRI values are kept. If the coefficient of variation (standard deviation divided by the mean) is below 2 percent, the pattern is treated as constant. Otherwise the PRIs are clustered with a tolerance of 3 percent of the mean, and the clusters that hold at least 15 percent of all pulses are kept. If 2 to 8 of these clusters remain, the pattern is marked as stagger, otherwise as jitter. At least 6 pulses are needed for classification.

### 3.4 Direction finding (planned)

For an array of M antennas and K snapshot vectors x[n], the covariance matrix is

$$R_{xx} = \frac{1}{K}\sum_{n=1}^{K}\mathbf{x}[n]\,\mathbf{x}^{H}[n]$$

Taking the noise subspace U_N from the eigenvalue decomposition, the MUSIC spectrum is

$$P(\theta) = \frac{1}{\mathbf{a}^{H}(\theta)\,U_N U_N^{H}\,\mathbf{a}(\theta)}$$

where a(θ) is the steering vector. The peaks give the angles of arrival. For a uniform linear array, Root-MUSIC finds the angle directly from the roots of a polynomial built from the same subspace, taking the roots closest to the unit circle. A separate calibration step is needed for the phase and gain differences between antennas.

## 4. Integration with SDR++

Each source in SDR++ provides a single IQ stream. The module takes its own VFO from this stream and reads the VFO output in a thread. The interface lives in the menu and in dock panels.

A few design decisions:

- The output sample rate of the VFO is kept equal to its bandwidth. The detector computes PW and TOA from this rate, so letting the rate and the bandwidth drift apart would corrupt the measurement.
- The VFO bandwidth cannot exceed the current spectrum of the source.
- The module's VFO is not selected by clicking on the waterfall. Its bandwidth is usually close to the whole spectrum, so it used to swallow the clicks meant for the other VFOs. It can be selected from the VFO list in the top bar or from the module's menu.

Direction finding needs several simultaneous channels, and SDR++ sources do not provide this at the moment. The options are to combine the streams of several receivers that share a clock, or to use a ready-made multi-channel receiver. Which one to use is not decided yet.

## 5. Evaluation method

This section describes measurements that have not been done yet.

Synthetic signal: IQ files generated with a known PW, frequency offset, SNR and PRI pattern. The following will be measured:

- Detection probability and false alarm rate as a function of SNR and the threshold margin (dB).
- PW, TOA and frequency error as a function of SNR. For frequency, a comparison with and without interpolation.
- Accuracy of the PRI classification for constant, stagger and jitter sequences.
- For DOA, the root mean square error (RMSE) as a function of SNR, number of antennas and angular separation.

Real signal: ADS-B messages at 1090 MHz can be received with an RTL-SDR. Their pulses are 0.5 µs wide and have a known format, which makes them a suitable test with a real pulsed signal.

Preliminary observation: with an RTL-SDR in the FM band (gain 32.8 dB), the power floor estimated from the samples outside pulses came out at about −30 dBFS. This value also contains the power of the FM broadcasts, so it is not plain noise. With a fixed −40 dBFS threshold, about 275 thousand "pulses" were counted in three seconds. With the automatic threshold (12 dB margin), the counter stayed at 481 after the first few tens of milliseconds at startup, with no new counts for at least 10 seconds. This is a single trial. It does not replace the planned measurements above.

## 6. Limitations

- Time resolution is limited by the sample rate. At 2.4 MS/s one sample is about 0.42 µs, so a 0.5 µs pulse is 1 to 2 samples long. PW and TOA are coarse because of this.
- Zero-padding and interpolation add no new information to the frequency resolution. In the end the pulse duration and the SNR set the frequency estimation accuracy.
- The RTL-SDR is 8 bit. Its dynamic range is limited, and weak pulses next to strong signals may not be visible.
- The detector is a simple energy detector with an amplitude threshold. It can give false alarms on signals whose envelope varies, such as continuous AM.
- Pulses come from a single VFO and a single channel. There is no separation of multiple emitters.
- Direction finding cannot be done with a single channel.
- The pulse log is written to CSV, and the file path is currently fixed in the code (`recordings/pdw_log.csv`).

## 7. Timeline

The first stage is the core of the pulse analysis: detector, parameter extractor, PRI analysis, table and histograms.

The second stage is validation: a synthetic signal generator, the measurements in section 5, and a real test with ADS-B. Depending on the results, the detector and the extractor may be tuned again.

The third stage is data: choosing the CSV path from the interface, recording with SigMF [4], a pulses per second display, and a power-frequency scatter plot.

The fourth stage is direction finding. First it will be decided how to get the multi-channel input, then covariance and MUSIC, Root-MUSIC, calibration and a direction display will follow.

## References

[1] gr-pdw. https://github.com/gtri/gr-pdw (GPLv3)

[2] gr-doa. https://github.com/EttusResearch/gr-doa (GPLv3)

[3] SDR++. https://github.com/AlexandreRouma/SDRPlusPlus

[4] SigMF. https://github.com/sigmf/SigMF

[5] R. O. Schmidt, "Multiple emitter location and signal parameter estimation," IEEE Trans. Antennas Propag., vol. 34, no. 3, pp. 276–280, 1986. doi:10.1109/TAP.1986.1143830

[6] A. J. Barabell, "Improving the resolution performance of eigenstructure-based direction-finding algorithms," ICASSP '83, vol. 8, pp. 336–339, 1983. doi:10.1109/ICASSP.1983.1172124

[7] H. K. Mardia, "New techniques for the deinterleaving of repetitive sequences," IEE Proc. F, vol. 136, no. 4, 1989. doi:10.1049/ip-f-2.1989.0025

[8] D. J. Milojević, B. M. Popović, "Improved algorithm for the deinterleaving of radar pulses," IEE Proc. F, vol. 139, no. 1, 1992. doi:10.1049/ip-f-2.1992.0012
