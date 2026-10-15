#!/usr/bin/env python3
"""
SDRSentinel rf_tools PDW Binary File Reader and HDF5 Converter

The HDF5 output follows the layout of GTRI gr-pdw (python/pdw_to_file.py): the attributes
time_unix, time_py, samp_rate, ref_level and the 7 datasets pulse_width_samps, pulse_width_secs,
pulse_power, noise_power, freq_start, toa_course, toa_fine, all float64, chunked and resizable.
It is not bit-for-bit identical to a gr-pdw file. Fields gr-pdw does not have go in the "sdrpp"
group, and the datasets whose meaning differs from gr-pdw carry a "definition" attribute:
  - time_unix is the wall-clock Unix time of detector sample 0 (float seconds, not rounded);
    absolute TOA = time_unix + sdrpp/toa.
  - toa_course is floor(time_unix) + whole seconds of the stream-relative TOA, as gr-pdw's
    floor(t_sob) + int(time.time()), but time.time() is replaced by the detector start time.
  - pulse_power is the PEAK power of the pulse; gr-pdw uses the mid-pulse sample.
  - noise_power is the MEAN of 16 samples before the pulse; gr-pdw uses one sample after it.
  - Power is calibrated dBm when the file says so, otherwise dBFS.
Format version 1 files have no detector start time; the recording start second is used instead,
so absolute times from such files are only approximate.
"""

import sys
import struct
import datetime
import argparse

RECORD_FORMAT = "<QQqQddIddfffffdfBB"
RECORD_SIZE = struct.calcsize(RECORD_FORMAT)

def read_pdw_file(filepath):
    with open(filepath, "rb") as f:
        # Read header (sizeof PdwBinaryHeader = 8+4+4+4+8+8+4+1+8+8+24 = 81 bytes, struct packed)
        magic = f.read(8).decode('latin-1')
        if not magic.startswith("SDRPPPDW"):
            raise ValueError(f"Invalid magic number in file: {magic}")
        
        version, hdr_size, rec_size, samp_rate, center_freq, ref_level, is_cal, start_time = struct.unpack(
            "<IIIddfBq", f.read(4 + 4 + 4 + 8 + 8 + 4 + 1 + 8)
        )
        if version >= 2:
            time_base, = struct.unpack("<d", f.read(8))
            f.read(24) # reserved
        else:
            time_base = float(start_time) # v1 has no detector start time
            f.read(32) # reserved

        print(f"Header: version={version}, sample_rate={samp_rate} Hz, center_freq={center_freq/1e6} MHz")
        print(f"Ref Level={ref_level} dBm, is_calibrated={bool(is_cal)}, start_time={start_time}, time_base={time_base}")

        records = []
        while True:
            chunk = f.read(rec_size)
            if len(chunk) < rec_size:
                break
            (p_idx, s_idx, c_toa, f_toa, toa, pw, pw_samps,
             f_offset, abs_freq, p_pwr, a_pwr, n_pwr, snr, phase,
             pri, doa_az, has_doa, is_cal_p) = struct.unpack(RECORD_FORMAT, chunk)

            records.append({
                "pulse_index": p_idx,
                "sample_index": s_idx,
                "course_toa": c_toa,
                "fine_toa": f_toa,
                "toa": toa,
                "pulse_width_secs": pw,
                "pulse_width_samps": pw_samps,
                "freq_offset": f_offset,
                "absolute_freq": abs_freq,
                "pulse_power": p_pwr,
                "average_power": a_pwr,
                "noise_power": n_pwr,
                "snr": snr,
                "pri": pri,
                "phase": phase,
                "doa_azimuth": doa_az if has_doa else None
            })

    print(f"Loaded {len(records)} pulses successfully.")
    return {
        "metadata": {
            "samp_rate": samp_rate,
            "center_freq": center_freq,
            "ref_level": ref_level,
            "time_unix": start_time,
            "time_base_unix": time_base,
            "version": version,
            "is_calibrated": bool(is_cal)
        },
        "records": records
    }

def convert_to_hdf5(pdw_data, h5_filepath):
    try:
        import h5py
        import numpy as np
    except ImportError as e:
        sys.stderr.write(f"Error: {e}. h5py and numpy are required to export to HDF5.\n")
        sys.exit(1)

    meta = pdw_data["metadata"]
    recs = pdw_data["records"]
    base = meta["time_base_unix"]

    def col(key):
        return np.array([r[key] for r in recs], dtype=np.float64)

    def put(group, name, values, definition=None):
        ds = group.create_dataset(name, data=values, dtype=np.float64, chunks=(1024,), maxshape=(None,))
        if definition:
            ds.attrs["definition"] = definition

    with h5py.File(h5_filepath, "w", track_order=True) as f:
        # Attributes as in gr-pdw pdw_to_file.py
        f.attrs["time_unix"] = float(base)
        f.attrs["time_py"] = datetime.datetime.fromtimestamp(base, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        f.attrs["samp_rate"] = meta["samp_rate"]
        f.attrs["ref_level"] = meta["ref_level"]

        # Datasets in the same names and order as gr-pdw
        put(f, "pulse_width_samps", col("pulse_width_samps"))
        put(f, "pulse_width_secs", col("pulse_width_secs"))
        put(f, "pulse_power", col("pulse_power"),
            "peak power of the pulse (gr-pdw: mid-pulse sample), dBm if calibrated else dBFS")
        put(f, "noise_power", col("noise_power"),
            "mean of 16 samples before the pulse (gr-pdw: last of 7 samples after it), dBm if calibrated else dBFS")
        put(f, "freq_start", col("freq_offset"))
        put(f, "toa_course", np.floor(base) + col("course_toa"),
            "floor(time_unix) + whole seconds of the stream-relative TOA (wall-clock seconds, as gr-pdw)")
        put(f, "toa_fine", col("fine_toa"),
            "samples into the stream-relative second, round(frac(toa) * samp_rate)")

        # Fields gr-pdw does not have
        g = f.create_group("sdrpp", track_order=True)
        g.attrs["center_freq"] = meta["center_freq"]
        g.attrs["is_calibrated"] = meta["is_calibrated"]
        g.attrs["binary_format_version"] = meta["version"]
        put(g, "pulse_index", col("pulse_index"))
        put(g, "sample_index", col("sample_index"))
        put(g, "toa", col("toa"), "seconds since detector sample 0; absolute TOA = time_unix + toa")
        put(g, "absolute_freq", col("absolute_freq"))
        put(g, "average_power", col("average_power"))
        put(g, "snr", col("snr"))
        put(g, "pri", col("pri"))
        put(g, "doa_azimuth", np.array([np.nan if r["doa_azimuth"] is None else r["doa_azimuth"] for r in recs], dtype=np.float64),
            "degrees, NaN when no DOA was estimated")

    print(f"Successfully exported {len(recs)} pulses to {h5_filepath} (gr-pdw style HDF5 layout, see script docstring for differences).")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Read SDRSentinel PDW Binary and optionally export to HDF5")
    parser.add_argument("input_pdwbin", help="Path to .pdwbin file")
    parser.add_argument("--to-h5", dest="output_h5", help="Export to a gr-pdw style HDF5 file")
    args = parser.parse_args()

    data = read_pdw_file(args.input_pdwbin)
    if args.output_h5:
        convert_to_hdf5(data, args.output_h5)
