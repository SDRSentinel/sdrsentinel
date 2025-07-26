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

## 8. Known issues and fix plan

A test session with an RTL-SDR (RTL2832U, R828D tuner) on the FM band and at 433.92 MHz, together with a review of the matched filter, VNA and calibration commits, found the problems below. Unit tests all pass; these issues show up only with live input or in the interface. They are listed in the order they will be fixed.

### 8.1 Matched filter runs on the DSP thread

`ScopePanel::loadPulse` is called from the pulse detector callback. It calls `updateMatchedFilter()` while holding the panel mutex, and that builds a new `ChirpMatchedFilter`, and therefore new FFTW plans, for every pulse. The FFTW planner is not thread safe and the waterfall plans FFTs on the GUI thread, so this can crash. The GUI also waits on the same mutex while the compression runs.

Fix: `loadPulse` only stores the pulse and marks the compression result as stale. The compression runs on the GUI thread inside `draw()`, only when the result is stale and the matched filter tab is visible, with one `ChirpMatchedFilter` kept as a member and reused.

### 8.2 Scope shows a pulse count but no pulse

The signal inspector shows "Captured Pulses: N" and "Awaiting pulse capture" at the same time. The MOP analyzer returns an empty envelope for pulses shorter than 8 samples, and the panel treats that as no pulse.

Fix: count only pulses the analyzer can use, and when the last pulse was too short, say so and show its length in samples.

### 8.3 Noise triggers with the default minimum width

The default minimum pulse width is 0.5 us, about one sample at 2.4 MS/s. With auto threshold at 12 dB above noise, single noise spikes are reported as pulses, about 10 per second on an RTL-SDR. Setting the minimum width to 20 us removed them.

Fix: enforce a minimum pulse length of a few samples in the detector, independent of the sample rate, and raise the default minimum width.

### 8.4 One-time out-of-range warning in power calibration

The B210 out-of-range warning in `PowerCalibrationTable::getRefLevelDbm` uses a function-local static flag. It fires once per process for all tables and never again after the table or the frequency changes.

Fix: keep the flag as a member and reset it when a new table is loaded.

### 8.5 PDW round-trip test depends on the working directory

`test_pdw_roundtrip.py` reads `test_output/pdw_test.pdwbin` relative to the current directory and fails with an import error when numpy or h5py are missing.

Fix: take the file path as an argument with a sensible default, and exit with a clear skip message when the Python dependencies are missing.

### 8.6 Clicks in floating windows reach the waterfall

Clicking or dragging inside a floating module window that lies over the waterfall also moves the VFO or the center frequency. Every click on the RF Tools tab bar changed the frequency during the test.

Fix: the waterfall should ignore mouse input when another window is hovered or active, using ImGui's hover state instead of only checking the waterfall rectangle.

## 9. root_dev integration audit

The commits that "integrated" components from `root_dev/` (gr-pdw, gr-doa, GPU_SDR) were checked line by line against the reference sources. Only the gr-doa math (ULA steering vector, spectral MUSIC, Root-MUSIC, covariance, subspace pilot calibration) is a real port. The other claims are either not supported by the code or contradicted by it, and the tests that back them compare the code with its own synthetic data. The problems are listed below, grouped in the order they will be fixed.

### 9.1 Power calibration does not use the GTRI data

The default "B210 GTRI curve" in `PowerCalibrationTable::loadDefaultB210` is a made-up linear formula, `6 - 0.86 g + 1.5 log10(f / 1 MHz)` dBm. At 1 GHz and 40 dB gain the GTRI table gives -27.02 dBm and the formula gives -23.91 dBm, and the real curve is strongly non-linear in gain, so the error exceeds 15 dB at some settings. `b210_cal.json` is an exact conversion of GTRI's `b210_pdw.pickle`, but it contains 52 bare `NaN` tokens, which the bundled JSON parser rejects, so the real table never loads and the formula stays active without any visible error. `calibrate()` also ignores the selected channel and always uses channel 0.

Fix: make the JSON valid (missing points as `null`), skip missing points when interpolating, load the GTRI table by default from the module resources, remove the formula, honor the selected channel, and test the loaded values against reference points taken from the GTRI pickle. Credit GTRI for the data.

### 9.2 PDW export metadata is wrong

The binary, JSON and SigMF exports write a sample rate of 2 MS/s whatever the real rate is. Pulse times are relative to the start of the detector, while the file header stores the wall-clock time at which recording started, so absolute times cannot be recovered. The HDF5 file written by `read_sdrpp_pdw.py` is described as the gr-pdw schema, but its attributes, its set of datasets, its data types and its fixed-size datasets all differ from `pdw_to_file.py`.

Fix: pass the real sample rate, store the detector start time so absolute TOA can be rebuilt, and write the gr-pdw attributes and datasets (float64, resizable) with the same names, keeping fields gr-pdw does not have in a separate group. Remaining differences in the meaning of `toa_course`, `pulse_power` and `noise_power` are documented instead of hidden.

### 9.3 Pulse width and noise estimate

The sample that ends a pulse (already below threshold) is counted in the pulse, so widths read one or more samples long. The rising-edge sample is pushed into the pre-pulse noise window before the pulse starts, which biases the noise floor and the SNR.

Fix: end the pulse at the last sample above the release threshold and keep the edge sample out of the noise window.

### 9.4 Direction finding thread safety

`ArrayCalibration::applyCorrection` reads the weights and the enabled flag without the mutex while the interface can reset, enable or reload them. The pilot calibration button and the worker thread both call `getNormalizedCovariance`, which consumes the matrix, so the button almost never gets one. The live module never calls `estimateFromSnapshots`. `music_engine.cpp` contains close translations of gr-doa code without a notice.

Fix: lock or snapshot the weights in `applyCorrection`, let the button read the latest covariance without consuming it, and add the gr-doa attribution.

### 9.5 Claims in documentation and code

The README, headers and test output claim things the code does not do: "GPU_SDR port" for the chirp matched filter and the VNA analyzer (GPU_SDR uses a stepped DDS chirp with a dechirp mixer and a complex nonlinear S21 fit, and has no `fit_resonator`), "JPL" labels, "CFAR" for a fixed-margin threshold with hysteresis, "< 0.05 deg" residual for a test with a 2.6 deg tolerance, "bit-for-bit GTRI datasets" for a self-referential round trip, and a "hardware status matrix" of PASS lines for synthetic tests. `dechirpMix` is never called.

Fix: describe each component as what it is, remove the dead code and the false labels, tighten or reword test checks so the printed claims match what is asserted, and add a credits and license section for gr-pdw (GPLv3, GTRI), gr-doa (GPLv3) and GPU_SDR (Apache-2.0).

## 10. Direction finding with a KrakenSDR

### 10.1 Current state

The direction finding and beamforming tabs of `rf_tools` do not show measured data. The default source is `ArraySynthesizer`, which generates a synthetic four-element array, so the bearing display, the beamformer and the per-pulse DOA in the PDW table all show simulated angles whatever receiver is connected. The "network stream" input, described as the KrakenSDR Heimdall format, expects a made-up UDP format (`CRDF` magic) and cannot read what a KrakenSDR sends. The "single channel" mode puts one receiver into channel 0 of the array and zeros into the others, which gives no bearing information. A single SDR cannot measure direction of arrival with MUSIC: that needs several channels sampled at the same time from a shared clock and local oscillator, with phase calibration.

### 10.2 Hardware and topology

A KrakenSDR has five RTL-SDR receivers on a shared clock and an internal noise source. Its DAQ software (heimdall_daq_fw, GPLv3) runs only on Linux (Raspberry Pi 4/5 or x86_64). It aligns sample delay, phase and amplitude of the channels using the noise source and serves the result over TCP:

- port 5000: coherent IQ frames, 1024-byte header plus complex float32 samples, channel-major, pulled by the client one frame at a time;
- port 5001: control, 128-byte commands for center frequency, gain and AGC.

SDR++ runs on any machine on the same network and connects as a client. At the default 2.4 MS/s and five channels the stream is about 768 Mbit/s (96 MB/s), so the link must be wired gigabit Ethernet, or the DAQ must decimate. The wire protocol is documented in `misc_modules/kraken_doa/HEIMDALL_PROTOCOL.md`.

### 10.3 Plan

1. Remove direction finding, beamforming and the array simulator from `rf_tools`. The PDW DOA field stays empty.
2. Add a new module, `kraken_doa`, with:
   - a Heimdall IQ client and control client implementing the protocol exactly;
   - frame gating: only DATA frames with both sync flags set are used; calibration, dummy and unsynchronized frames are counted and shown, never processed;
   - channel selection inside the frame (frequency offset and bandwidth) with a squelch, as in the reference client;
   - Bartlett, Capon and MUSIC on a uniform circular or uniform linear array, geometry given in meters, bearing reported as in the reference client;
   - an interface that shows connection and calibration state at all times and draws a bearing only from a usable frame. There is no simulator and no placeholder data in the interface.
3. Tests feed byte-exact Heimdall frames through a local TCP server inside the test and check header parsing, gating and bearings of plane waves with known angles. These are software tests; they do not replace a check with real hardware.

### 10.4 Limits

- Validation with a real KrakenSDR has not been done yet; the first hardware run must compare bearings with a known transmitter position.
- A uniform linear array cannot tell front from back; only a circular array gives 0 to 360 degrees.
- Multipath, especially indoors, can move or split the peak; a single bearing is not a position.

## References

[1] gr-pdw. https://github.com/gtri/gr-pdw (GPLv3)

[2] gr-doa. https://github.com/EttusResearch/gr-doa (GPLv3)

[3] SDR++. https://github.com/AlexandreRouma/SDRPlusPlus

[4] SigMF. https://github.com/sigmf/SigMF

[5] R. O. Schmidt, "Multiple emitter location and signal parameter estimation," IEEE Trans. Antennas Propag., vol. 34, no. 3, pp. 276–280, 1986. doi:10.1109/TAP.1986.1143830

[6] A. J. Barabell, "Improving the resolution performance of eigenstructure-based direction-finding algorithms," ICASSP '83, vol. 8, pp. 336–339, 1983. doi:10.1109/ICASSP.1983.1172124

[7] H. K. Mardia, "New techniques for the deinterleaving of repetitive sequences," IEE Proc. F, vol. 136, no. 4, 1989. doi:10.1049/ip-f-2.1989.0025

[8] D. J. Milojević, B. M. Popović, "Improved algorithm for the deinterleaving of radar pulses," IEE Proc. F, vol. 139, no. 1, 1992. doi:10.1049/ip-f-2.1992.0012
