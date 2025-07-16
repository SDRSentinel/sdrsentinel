# kraken_doa

Direction finding with a KrakenSDR in SDR++. The module is a client of the KrakenSDR DAQ software: it reads coherent IQ frames over TCP, estimates the direction of arrival of one signal with Bartlett, Capon or MUSIC, and shows the bearing. It runs on any machine that can reach the DAQ over the network.

## What it does

- Connects to the Heimdall IQ server (TCP port 5000) and optionally to the control server (port 5001). The wire protocol is described in `HEIMDALL_PROTOCOL.md`.
- Uses a frame only when its sync word is correct, it is a DATA frame, and both `delay_sync_flag` and `iq_sync_flag` are 1. Calibration, dummy and unsynchronized frames are counted and explained, never processed.
- Selects a channel inside the frame (frequency offset and bandwidth), applies a squelch, and computes the spectrum over 0..359 degrees.
- Supports a uniform circular array (UCA, radius in meters) and a uniform linear array (ULA, spacing in meters), with an array offset and a compass offset. The bearing is reported as in the reference client: `(360 - theta_peak + compass_offset) mod 360`.
- Draws a compass plot, a spectrum plot, the bearing, the peak quality, the MUSIC eigenvalues and a history table.

## What it does not do

- It shows no simulated, demo or placeholder data. Until a usable frame is processed the page shows a status line that says why (not connected, waiting for the first frame, DAQ calibrating, frame not usable, channel count mismatch, below squelch, last bearing stale).
- It does not talk to the receivers directly and does not replace the DAQ. It cannot work from a single SDR.
- It estimates one bearing, not a position, and does not estimate the number of sources (set it for MUSIC).
- It does not use the SDR++ source or the waterfall. The signal is chosen by offset and bandwidth relative to the DAQ center frequency.

## Setup

1. KrakenSDR hardware connected to a Linux host (Raspberry Pi 4/5 or x86_64) running `heimdall_daq_fw`. The DAQ must run with its TCP IQ server enabled on port 5000 and `hw_controller` on port 5001 (needed only for frequency and gain control). The exact startup option or script that enables the TCP IQ server differs between DAQ images: verify on your DAQ image.
2. Network. The raw sample stream at the defaults (2.4 MS/s, five channels, complex float32 = 8 bytes per sample) is 2.4e6 x 5 x 8 x 8 = 768 Mbit/s before protocol overhead. Either way the stream needs wired gigabit Ethernet on both ends, or a higher DAQ decimation (the client reads `sampling_freq` and `cpi_length` from every header). Wi-Fi and 100 Mbit links will not keep up. The page shows the measured throughput and warns about frames dropped by the DAQ (cpi gaps) or overwritten here.
3. In SDR++ open "KrakenSDR DOA", enter the DAQ host and port, press Connect. Tick "Control the DAQ" only if you want to set frequency and gain from SDR++ (control port 5001, no authentication, one client at a time).
4. Enter the array: type, number of elements (5 for a KrakenSDR), radius (UCA) or spacing (ULA) in meters, array offset and compass offset. The page shows the size in wavelengths at the current frequency and warns when the spacing exceeds half a wavelength (spatial aliasing).
5. Set the signal offset and bandwidth (kHz, relative to the DAQ center frequency) and a squelch level in dBFS.

## Frequency and gain control

With "Control the DAQ" on and connected: enter the center frequency in MHz and press Apply. The DAQ retunes and restarts calibration; the page says it is recalibrating until the sync flags return, and no bearing is drawn meanwhile. Gain: choose one of the gains the DAQ accepts (applied to all channels, the channel count comes from the last frame header; disabled until a header has arrived). AGC enables the hardware AGC. Commands run in the background; a failed command shows its error. Changing frequency or gain on the DAQ's own web interface also works; the page follows the frame headers.

## Reading the status panel

- Sync state: INIT, sample cal, fractional cal, IQ cal, track lock, track. Both sync flags must be 1 for bearings.
- Noise source ON: the DAQ is calibrating; CAL frames are not used.
- ADC overdrive: a channel marked red with `!` is clipping; lower the gain.
- IF gains: the gain per channel from the header, in dB.
- Frames by type, cpi gaps (frames the DAQ dropped upstream), overwritten (frames received faster than processed; only the newest is used).
- Throughput in Mbit/s and processing time per frame.

A bearing is drawn only when the last result is fresh: computed from a frame received within the last max(5 s, 3 x the smoothed frame period). Older results are not drawn; the status line says the bearing is stale and why.

## Limits

- Not yet validated on real hardware. The first hardware run must compare bearings with a transmitter at a known position. The software tests use a fake Heimdall server and synthetic plane waves; they do not replace that check.
- A ULA cannot tell front from back. Only a circular array gives 0 to 360 degrees.
- Multipath, especially indoors, can move or split the peak. A bearing is not a position.
- Phase calibration is done by the DAQ; this module uses the samples as delivered.
- The unit of the header time stamp is not confirmed; the page shows it raw.

## Tests

`test_kraken_heimdall_header`, `test_kraken_heimdall_client`, `test_kraken_doa_dsp` and `test_kraken_doa_pipeline` are built next to the module. The pipeline test starts a fake Heimdall server on 127.0.0.1 inside the test; it is not part of the module.

## Credits

The protocol and the reference processing were implemented from the sources of:

- heimdall_daq_fw (GPLv3), Copyright Tamas Peto.
- krakensdr_doa (GPLv3), Copyright Carl Laufer, Tamas Peto.

No code from them is copied into this module.
