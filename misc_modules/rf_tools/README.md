# RF Tools Suite for SDR++ (`misc_modules/rf_tools`)

Comprehensive Radar Electronic Warfare (EW) pulse analysis, Pulse Modulation on Pulse (MOP), and Vector Network Analyzer (VNA) suite integrated directly into SDR++.

`rf_tools` is a set of C++ DSP engines with ImGui panels. The B210 power calibration data and the HDF5 layout come from Georgia Tech Research Institute's **`gr-pdw`**; the pulse detector and PDW extractor are an independent design inspired by it. The chirp matched filter, resonator analysis and MOP analysis are written for this module. See "Credits and licenses" below.

Direction finding is not part of `rf_tools`. It is in the `kraken_doa` module (`misc_modules/kraken_doa`) and needs a KrakenSDR (a coherent 5-channel receiver).

---

## Architecture Overview

```
SDR++ Core VFO Stream [IQ @ Fs]
      │
      ├──> [PulseDetector] ──────> Threshold Edge Detection (noise margin + hysteresis)
      │         │
      │         ▼
      │   [PulseExtractor] ──────> TOA (Coarse/Fine), PW, Pk/Avg Power, SNR, Freq Offset
      │         │                  └── PowerCalibrationTable (USRP B210 GTRI data, B210 only)
      │         ├──> [PdwBinaryExporter] ──> High-speed .pdwbin recording (own binary format)
      │         │                             └── tools/read_sdrpp_pdw.py ──> HDF5 (.h5)
      │         ├──> [PRIAnalyzer] ────────> Histogram, Stagger, Jitter & PRI Agility
      │         └──> [EmitterTracker] ─────> Cluster by Frequency, PW & PRI
      │
      ├──> [ScopePanel / MOPAnalyzer] ────> Intra-pulse Envelope, Phase, Inst. Freq
      │         │                           ├── Barker 7/11/13 & Frank Phase Codes
      │         │                           ├── STFT Spectrogram & IQ Orbit
      │         │                           └── [ChirpMatchedFilter]
      │         │                                 ├── Linear Frequency Modulation (LFM)
      │         │                                 ├── FFT Fast Convolution Compression
      │         │                                 ├── Sidelobe Windowing (Hamming/Hann)
      │         │                                 └── Live Compressed Envelope Visualizer
      │
      └──> [VNAAnalyzer] ───────────> S21 magnitude trace (synthetic sweep only)
                                              ├── Resonator dip/peak width from |S21| in dB
                                              ├── Loaded Q (Q_L), Internal Q (Q_i), Coupling Q (Q_c)
                                              └── VNAPanel Live Transmission Readout
```

---

## Feature Modules

### 1. Radar Pulse Detection & PDW Extraction (inspired by `gr-pdw`)
* **Real-Time Edge Detection:** fixed threshold, or a fixed dB margin over an exponential-average noise estimate, with 3 dB hysteresis and min/max pulse width gating. This is not a CFAR detector.
* **Comprehensive PDW Fields:**
  * Coarse TOA (seconds) and Fine TOA (sub-microsecond samples relative to PPS/epoch).
  * Pulse Width ($\mu\text{s}$ and samples).
  * Carrier frequency offset and absolute RF frequency.
  * Peak and average power (dBFS and calibrated dBm), noise floor, and SNR.
* **USRP B210 Power Calibration:**
  * GTRI B210 calibration data (from `b210_pdw.pickle` of gr-pdw, stored in `src/pdw/b210_cal.json`) mapping hardware gain ($0 - 80\text{ dB}$) and frequency ($70\text{ MHz} - 6.0\text{ GHz}$) to $0\text{ dBFS}$ reference level in $\text{dBm}$.
  * **Valid only for an Ettus USRP B210.** The table was measured by GTRI on B210 hardware. With any other receiver (RTL-SDR, HackRF, Airspy, other USRP models) the dBm values it produces are wrong; use dBFS, or load a table measured for your own receiver with "Load JSON...". The panel says this permanently and shows a red warning when calibration is enabled with the default table while the selected SDR++ source is not "USRP". Even on a B210, individual units differ from the GTRI unit, so treat the values as approximate.
  * Enforces official Analog Devices AD9361 RFIC frequency specifications with automated out-of-range warnings for legacy $50-70\text{ MHz}$ data.
* **Streaming Exporter:**
  * Binary serialization (`.pdwbin`) with a fixed 81-byte header and 102-byte pulse records.
  * Standalone Python converter (`tools/read_sdrpp_pdw.py`) writing HDF5 (`.h5`) with the gr-pdw dataset names and dtypes. It is not identical to a gr-pdw file: `pulse_power` and `noise_power` are defined differently (see the docstring of the script), and SDR++-only fields go in a separate `sdrpp` group.

### 2. Signal Inspector (MOP) & Live Chirp Matched Filter
* **Intra-Pulse Modulation on Pulse (MOP):**
  * Instantaneous frequency derivation with Savitzky-Golay / moving polynomial smoothing.
  * Linear regression slope ($k = \Delta f / \Delta t$) with $R^2$ linearity scoring.
  * Phase unwrapping and cross-correlation detection for Barker-7, Barker-11, and Barker-13 phase codes.
  * Short-Time Fourier Transform (STFT) spectrogram with Turbo colormap and I/Q constellation orbit.
* **Live Matched Filter & Pulse Compression Visualizer (`ScopePanel`):**
  * Generic continuous-LFM FFT matched filter: impulse response $h[n] = w[n] \cdot s_{\text{ref}}^*[N-1-n]$ applied by FFT fast convolution. It is not derived from GPU_SDR, which uses a stepped-frequency chirp and a sample-wise dechirp mixer for VNA lock-in.
  * **Interactive Visualizer Canvas:**
    * Displays normalized compressed pulse envelope $|y[n]|$ with translucent area fill.
    * Toggle between **dB Logarithmic Scale** ($0\text{ dB}$ to $-50\text{ dB}$) and **Linear Magnitude** ($0.0$ to $1.0$).
    * Zoom mode focusing on mainlobe peak ($\pm 35$ samples) or full linear convolution span ($2N-1$ samples).
    * Reference grid lines at $0\text{ dB}$, $-3\text{ dB}$ (half-power resolution), $-13.2\text{ dB}$ (theoretical sinc first sidelobe), and $-40\text{ dB}$.
  * **Sidelobe Suppression Windowing:**
    * **Rectangular (Uniform):** Narrowest mainlobe width, theoretical sinc sidelobes ($-13.26\text{ dB}$).
    * **Hamming (Radar Default):** Sidelobe suppression down to $-40.3\text{ dB}$ with minimal mainlobe broadening.
    * **Hann (Cosine-squared):** Smooth rolloff for spectral containment.
  * **Real-Time Performance Metrics:**
    * Compression Ratio ($T / \tau_{\text{comp}}$), e.g. $50.0\times$.
    * Peak Compressed SNR Processing Gain ($10\log_{10}(BT)$), e.g. $+19.0\text{ dB}$.
    * Peak-to-Sidelobe Ratio (PSLR in dB) measured outside the first mainlobe null.
    * Manual / Force Slope mode allowing real-time parameter tweaking for unknown or low-SNR chirps.

### 3. Resonator Analysis of an S21 Magnitude Trace (`VNAAnalyzer`)
* There is no hardware sweep path: the trace comes from a synthetic generator (`generateSyntheticResonatorSweep`) or `setFullTrace`. This is not a port of GPU_SDR, whose resonator fit is a complex nonlinear least-squares fit.
* **Width-based resonator estimate:**
  * The generator uses the canonical Lorentzian notch model:
    $$S_{21}(f) = 1 - \frac{Q_L / Q_c}{1 + 2 j Q_L \frac{f - f_0}{f_0}}, \quad \frac{1}{Q_L} = \frac{1}{Q_i} + \frac{1}{Q_c}$$
  * Minimum transmission magnitude: $|S_{21}(f_0)| = \frac{Q_L}{Q_i} = 10^{-\text{depth}/20}$.
  * The analyzer reads only $|S_{21}|$ in dB (phase, cable delay, asymmetry and nonlinearity are ignored). It finds the half-power width at:
    $$P_{\text{target}} = \frac{P_{\text{baseline}} + P_{\min}}{2}$$
  * From the width and the dip depth it computes $Q_L = f_0 / \text{BW}$, $Q_i = Q_L / S_{21,\min}$ and $Q_c = Q_L / (1 - S_{21,\min})$. On the synthetic notch in the test these are within $2\%$ of the generating values (the generator and the analyzer use the same model, so this checks self-consistency only).

---

## Test & Verification Matrix

The module includes comprehensive automated C++ and Python verification test suites in `tests/`:

| Test Suite / Algorithm | Command | Status | What is asserted |
| :--- | :--- | :---: | :--- |
| **Power calibration** | `./build/misc_modules/rf_tools/test_root_dev_integration` | **PASS** | 10 reference points computed from GTRI's `b210_pdw.pickle` within 0.05 dB, channel selection, no NaN over the table range, AD9361 range flag |
| **Binary serialization** | same | **PASS** | 10 PDWs, file size = header + 10 records, header fields (sample rate, time base) read back |
| **Chirp AWGN gain** | same | **PASS** | In-band gain within 1.5 dB of $10\log_{10}(BT) = 19.03\text{ dB}$ at 0 and -6 dB input SNR |
| **Chirp windowing** | same | **PASS** | Unwindowed PSLR $< -12.5\text{ dB}$, Hamming PSLR $< -28\text{ dB}$ |
| **Chirp Doppler** | same | **PASS** | +50 kHz offset: compression ratio $> 40\times$ and PSLR within 3 dB of the zero-Doppler value (peak loss is not measured, the envelope is normalized) |
| **Resonator Q analysis** | same | **PASS** | $f_0$ within 1 kHz, $Q_L$, $Q_i$, $Q_c$ within 2% of the synthetic generator's parameters |
| **Pulse detector** | `./build/misc_modules/rf_tools/test_rf_tools_pulse_detector` | **PASS** | Pulse widths and noise estimate on synthetic pulses |
| **Python HDF5 round trip** | `uv run --no-project --with numpy --with h5py python misc_modules/rf_tools/tests/test_pdw_roundtrip.py` | **PASS** | C++ `.pdwbin` converted to HDF5 and read back; values equal the C++ input |
| **CPU throughput** | `./build/misc_modules/rf_tools/test_root_dev_integration` | **PASS** | Loose lower bounds only (matched filter $> 5000$ pulses/s) |
| **Physical USRP B210 OTA** | *Requires physical RF hardware* | **NOT TESTED** | |
| **Physical TwinRX LO sync** | *Requires physical RF hardware* | **NOT TESTED** | |

### What is validated

* Checked against external reference values: only the power calibration table (test 1, values taken from GTRI's `b210_pdw.pickle`).
* Everything else is self-consistency on synthetic data generated by the same code base (or by the test itself). This includes the matched filter, resonator and HDF5 tests. No test uses real RF captures or hardware, and nothing was compared against GPU_SDR output.
* The test executable is still named `test_root_dev_integration` for historical reasons.

---

## Building

The module builds automatically as part of SDR++:

```sh
cmake -B build -G Ninja
ninja -C build
```

Run test suite:
```sh
# C++ Integration and Unit Tests
./build/misc_modules/rf_tools/test_root_dev_integration
./build/misc_modules/rf_tools/test_rf_tools_pulse_detector

# Python PDW Binary -> HDF5 Round-Trip Test
uv run --no-project --with numpy --with h5py python misc_modules/rf_tools/tests/test_pdw_roundtrip.py
```

---

## Credits and licenses

* **gr-pdw** (https://github.com/gtri/gr-pdw, GPLv3, Copyright 2024 GTRI): the USRP B210 power calibration data (`src/pdw/b210_cal.json`, converted from `b210_pdw.pickle`) and the HDF5 dataset layout. The pulse detector and PDW extractor are an independent design inspired by gr-pdw, not a port.
* **GPU_SDR** (Apache-2.0): nothing in `rf_tools` is derived from it. The chirp matched filter and the resonator analysis were written independently of it.
