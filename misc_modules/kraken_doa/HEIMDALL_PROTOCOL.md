# Heimdall DAQ wire protocol (KrakenSDR)

This is the protocol `kraken_doa` implements. It was taken from the sources of heimdall_daq_fw (DAQ, GPLv3, Copyright 2018-2020 Tamas Peto) and krakensdr_doa (reference client, GPLv3, Copyright 2018-2021 Carl Laufer, Tamas Peto), `main` branches as of 2025. Paths below are in those repositories. No code from them is copied into this module.

The DAQ runs on Linux only. Both servers bind to all interfaces, accept one client at a time and have no authentication.

## 1. IQ data, TCP port 5000

The DAQ is the server (`Firmware/_daq_core/eth_server.h`, `iq_server.c`).

1. The client connects and sends the 9 ASCII bytes `streaming` (no terminator, no length). Any other string closes the connection.
2. The server sends one frame.
3. For every further frame the client sends the 10 ASCII bytes `IQDownload`, then reads one frame. Anything else ends the session; the reference client sends `q` and closes.

The server sends exactly one frame per request. It does not push. If the client is slow, the DAQ drops frames upstream (`cpi_index` jumps).

A frame is a 1024-byte header followed by `cpi_length * active_ant_chs * 8` bytes of payload. There is no trailer.

### Header (1024 bytes, little-endian, natural C alignment)

From `iq_header.h` (`IQ_HEADER_LENGTH 1024`) and `iq_header.py` (`struct` format without prefix). Offsets are computed from the struct, including two 4-byte alignment gaps.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0 | 4 | uint32 | sync_word | must be `0x2bf7b95a` |
| 4 | 4 | uint32 | frame_type | 0 DATA, 1 DUMMY, 2 RAMP, 3 CAL, 4 TRIGW |
| 8 | 16 | char[16] | hardware_id | e.g. `kraken5`, NUL padded |
| 24 | 4 | uint32 | unit_id | |
| 28 | 4 | uint32 | active_ant_chs | channel count M, 5 on a KrakenSDR |
| 32 | 4 | uint32 | ioo_type | |
| 36 | 4 | - | padding | |
| 40 | 8 | uint64 | rf_center_freq | Hz |
| 48 | 8 | uint64 | adc_sampling_freq | Hz |
| 56 | 8 | uint64 | sampling_freq | Hz after decimation |
| 64 | 4 | uint32 | cpi_length | samples per channel in this frame |
| 68 | 4 | - | padding | |
| 72 | 8 | uint64 | time_stamp | Unix time; the reference client treats it as milliseconds (unit not confirmed in the DAQ) |
| 80 | 4 | uint32 | daq_block_index | |
| 84 | 4 | uint32 | cpi_index | frame counter, gaps mean dropped frames |
| 88 | 8 | uint64 | ext_integration_cntr | |
| 96 | 4 | uint32 | data_type | 0 dummy, 1 raw, 2 IQ, 3 decimated IQ |
| 100 | 4 | uint32 | sample_bit_depth | 32 at the IQ server |
| 104 | 4 | uint32 | adc_overdrive_flags | bit per channel |
| 108 | 128 | uint32[32] | if_gains | dB x 10 per channel |
| 236 | 4 | uint32 | delay_sync_flag | sample delay aligned |
| 240 | 4 | uint32 | iq_sync_flag | phase and amplitude aligned |
| 244 | 4 | uint32 | sync_state | 1 INIT, 2 sample cal, 3 fractional cal, 4 IQ cal, 5 track lock, 6 track |
| 248 | 4 | uint32 | noise_source_state | 1 while the noise source is on |
| 252 | 768 | uint32[192] | reserved | |
| 1020 | 4 | uint32 | header_version | 7 |

A frame is usable for direction finding only when `sync_word` is correct, `frame_type` is 0 (DATA), `delay_sync_flag` is 1 and `iq_sync_flag` is 1. The DAQ's own recorder uses the same condition (`Firmware/_testing/iq_recorder.py`). The reference DOA client only checks `frame_type`; this module also requires the sync flags.

DUMMY frames have `cpi_length` 0 and no payload. CAL frames (noise source on) arrive in bursts of 10 every 687 data frames with the default `cal_track_mode`, and are not decimated, so `sampling_freq` and `cpi_length` must be read from every header.

### Payload

Complex float32 (I then Q, little-endian), channel-major: all `cpi_length` samples of channel 0, then channel 1, and so on (`fir_decimate.c`, reference client `reshape(active_ant_chs, cpi_length)`). Values are scaled to about [-1, 1). Phase and amplitude alignment is already applied by the DAQ (`delay_sync.py`), so the client uses the samples as they are.

Default configuration (`Firmware/daq_chain_config.ini`): 5 channels, 2.4 MS/s, `cpi_size` 1048576, `decimation_ratio` 1. That is about 42 MB per frame and 1.2 Gbit/s; use wired gigabit Ethernet or raise the decimation.

## 2. Control, TCP port 5001

The DAQ (`Firmware/hw_controller.py`) is the server. Every message is exactly 128 bytes: a 4-byte ASCII command and 124 bytes of payload, zero padded. Every command except `EXIT` is acknowledged with 128 bytes starting with `FNSD`, sent after the command has been applied.

| Command | Payload | Effect |
|---|---|---|
| `INIT` | none | no-op, acknowledged |
| `FREQ` | uint64 Hz at bytes 4..11 | retunes all channels; the DAQ restarts calibration and both sync flags drop until it relocks |
| `GAIN` | M x uint32 at bytes 4.., gain in dB x 10 | sets per-channel gain and turns AGC off; values must be in the list below or they are ignored |
| `AGC ` | none (note the space) | enables hardware AGC |
| `EXIT` | none | closes, no reply |

Accepted gains (dB x 10): 0, 9, 14, 27, 37, 77, 87, 125, 144, 157, 166, 197, 207, 229, 254, 280, 297, 328, 338, 364, 372, 386, 402, 421, 434, 439, 445, 480, 496.

## 3. Reference DOA processing

From `_sdr/_signal_processing/kraken_sdr_signal_processor.py`:

- Correlation matrix `R = X X^H / N` over the selected channel samples, no forward-backward averaging by default.
- Scan 0 to 359 degrees in 1 degree steps. Element m has position (x_m, y_m) in wavelengths and steering phase `a_m(theta) = exp(j 2 pi (x_m cos(theta + offset) + y_m sin(theta + offset)))`.
- ULA: `x_m = 0`, `y_m = -m d`. The spectrum is mirror symmetric (front/back ambiguity).
- UCA: radius r in wavelengths, `x_m = r cos(2 pi m / M)`, `y_m = -r sin(2 pi m / M)`.
- The user gives the ULA spacing or the UCA radius in meters; it is divided by the wavelength at the center frequency, and scaled by `f / f_center` for a channel at frequency f.
- Algorithms: Bartlett, Capon, MEM, MUSIC (default). MUSIC uses the `M - K` eigenvectors with the smallest eigenvalues as noise subspace, K = expected number of sources (default 1, not estimated), spectrum `1 / |a^H E E^H a|`.
- The displayed bearing is `(360 - theta_peak + compass_offset) mod 360`.
- DOA runs only when the selected channel's amplitude is above a squelch level.
