#!/usr/bin/env python3
"""
SDR++ rf_tools PDW Binary File Reader and HDF5 Converter
Compatible with GTRI gr-pdw dataset structure.
"""

import sys
import struct
import argparse

RECORD_FORMAT = "<QQqQddIddfffffdfBB"
RECORD_SIZE = struct.calcsize(RECORD_FORMAT)

def read_pdw_file(filepath):
    with open(filepath, "rb") as f:
        # Read header (sizeof PdwBinaryHeader = 8+4+4+4+8+8+4+1+8+32 = 81 bytes, struct packed)
        magic = f.read(8).decode('latin-1')
        if not magic.startswith("SDRPPPDW"):
            raise ValueError(f"Invalid magic number in file: {magic}")
        
        version, hdr_size, rec_size, samp_rate, center_freq, ref_level, is_cal, start_time = struct.unpack(
            "<IIIddfBq", f.read(4 + 4 + 4 + 8 + 8 + 4 + 1 + 8)
        )
        f.read(32) # reserved

        print(f"Header: version={version}, sample_rate={samp_rate} Hz, center_freq={center_freq/1e6} MHz")
        print(f"Ref Level={ref_level} dBm, is_calibrated={bool(is_cal)}, start_time={start_time}")

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
                "doa_azimuth": doa_az if has_doa else None
            })

    print(f"Loaded {len(records)} pulses successfully.")
    return {
        "metadata": {
            "samp_rate": samp_rate,
            "center_freq": center_freq,
            "ref_level": ref_level,
            "time_unix": start_time
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

    with h5py.File(h5_filepath, "w") as f:
        # Attributes matching gr-pdw
        f.attrs["samp_rate"] = meta["samp_rate"]
        f.attrs["ref_level"] = meta["ref_level"]
        f.attrs["time_unix"] = meta["time_unix"]
        f.attrs["center_freq"] = meta["center_freq"]

        # Datasets matching gr-pdw
        f.create_dataset("pulse_width_samps", data=np.array([r["pulse_width_samps"] for r in recs], dtype=np.uint32))
        f.create_dataset("pulse_width_secs", data=np.array([r["pulse_width_secs"] for r in recs], dtype=np.float64))
        f.create_dataset("pulse_power", data=np.array([r["pulse_power"] for r in recs], dtype=np.float32))
        f.create_dataset("noise_power", data=np.array([r["noise_power"] for r in recs], dtype=np.float32))
        f.create_dataset("freq_start", data=np.array([r["freq_offset"] for r in recs], dtype=np.float64))
        f.create_dataset("toa_course", data=np.array([r["course_toa"] for r in recs], dtype=np.int64))
        f.create_dataset("toa_fine", data=np.array([r["fine_toa"] for r in recs], dtype=np.uint64))
        f.create_dataset("pri", data=np.array([r["pri"] for r in recs], dtype=np.float64))

    print(f"Successfully exported {len(recs)} pulses to {h5_filepath} in GTRI gr-pdw HDF5 schema.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Read SDR++ PDW Binary and optionally export to HDF5")
    parser.add_argument("input_pdwbin", help="Path to .pdwbin file")
    parser.add_argument("--to-h5", dest="output_h5", help="Export to gr-pdw compatible HDF5 file")
    args = parser.parse_args()

    data = read_pdw_file(args.input_pdwbin)
    if args.output_h5:
        convert_to_hdf5(data, args.output_h5)
