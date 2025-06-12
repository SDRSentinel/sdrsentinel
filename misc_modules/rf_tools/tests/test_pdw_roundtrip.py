#!/usr/bin/env python3
"""
SDR++ rf_tools PDW C++ to Python Round-Trip and HDF5 Integrity Test
Validates binary serialization against GTRI gr-pdw schema.
"""

import sys
import os
import math
import numpy as np
import h5py

# Import reader from tools
tools_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, tools_dir)
from read_sdrpp_pdw import read_pdw_file, convert_to_hdf5

def main():
    print("============================================================")
    print("   rf_tools PDW Binary C++ -> Python Round-Trip Test        ")
    print("============================================================")

    pdwbin_path = "test_output/pdw_test.pdwbin"
    h5_path = "test_output/pdw_test.h5"

    if not os.path.exists(pdwbin_path):
        sys.stderr.write(f"ERROR: {pdwbin_path} does not exist. Run test_root_dev_integration first.\n")
        sys.exit(1)

    print(f"[TEST] Reading binary PDW stream from '{pdwbin_path}'...")
    data = read_pdw_file(pdwbin_path)

    meta = data["metadata"]
    records = data["records"]

    # 1. Validate Header
    assert math.isclose(meta["samp_rate"], 2.0e6, rel_tol=1e-5), f"Sample rate mismatch: {meta['samp_rate']}"
    assert math.isclose(meta["center_freq"], 433.92e6, rel_tol=1e-5), f"Center freq mismatch: {meta['center_freq']}"
    assert math.isclose(meta["ref_level"], -25.0, rel_tol=1e-4), f"Ref level mismatch: {meta['ref_level']}"
    print("  -> Header metadata asserted: 2.0 MSPS, 433.92 MHz, Ref -25 dBm: MATCH")

    # 2. Validate Records count and content
    assert len(records) == 10, f"Expected 10 records, got {len(records)}"
    for i, r in enumerate(records, start=1):
        assert r["pulse_index"] == i, f"Pulse index mismatch at {i}: {r['pulse_index']}"
        assert r["sample_index"] == i * 2000, f"Sample index mismatch at {i}"
        expected_toa = (i * 2000) / 2.0e6
        assert math.isclose(r["toa"], expected_toa, rel_tol=1e-6), f"TOA mismatch at {i}"
        assert r["course_toa"] == int(math.floor(expected_toa))
        assert math.isclose(r["pulse_width_secs"], 10.0e-6, rel_tol=1e-6)
        assert r["pulse_width_samps"] == 20
        assert math.isclose(r["pulse_power"], -40.0, rel_tol=1e-4) # Calibrated dBm
        assert math.isclose(r["average_power"], -43.0, rel_tol=1e-4)
        assert math.isclose(r["noise_power"], -95.0, rel_tol=1e-4)
        assert math.isclose(r["snr"], 52.0, rel_tol=1e-4)
        assert math.isclose(r["pri"], 0.001, rel_tol=1e-6)
    print("  -> All 10 pulse records deserialized and numerically verified: MATCH")

    # 3. Export to HDF5
    print(f"[TEST] Exporting to HDF5 file '{h5_path}'...")
    convert_to_hdf5(data, h5_path)
    assert os.path.exists(h5_path), f"HDF5 file was not created: {h5_path}"

    # 4. Read back HDF5 and verify GTRI gr-pdw schema
    print(f"[TEST] Verifying HDF5 schema and dataset integrity...")
    with h5py.File(h5_path, "r") as hf:
        assert math.isclose(hf.attrs["samp_rate"], 2.0e6, rel_tol=1e-5)
        assert math.isclose(hf.attrs["ref_level"], -25.0, rel_tol=1e-4)
        assert math.isclose(hf.attrs["center_freq"], 433.92e6, rel_tol=1e-5)

        required_datasets = [
            "pulse_width_samps", "pulse_width_secs", "pulse_power", "noise_power",
            "freq_start", "toa_course", "toa_fine", "pri"
        ]
        for ds_name in required_datasets:
            assert ds_name in hf, f"Missing dataset {ds_name} in HDF5"
            arr = hf[ds_name][:]
            assert len(arr) == 10, f"Dataset {ds_name} length {len(arr)} != 10"

        pw_samps = hf["pulse_width_samps"][:]
        pw_secs = hf["pulse_width_secs"][:]
        p_pwr = hf["pulse_power"][:]
        n_pwr = hf["noise_power"][:]
        pri = hf["pri"][:]

        np.testing.assert_array_equal(pw_samps, np.full(10, 20, dtype=np.uint32))
        np.testing.assert_allclose(pw_secs, 10.0e-6, rtol=1e-6)
        np.testing.assert_allclose(p_pwr, -40.0, rtol=1e-4)
        np.testing.assert_allclose(n_pwr, -95.0, rtol=1e-4)
        np.testing.assert_allclose(pri, 0.001, rtol=1e-6)

    print("  -> HDF5 datasets (8 fields x 10 records) verified against C++ ground truth: MATCH")
    print("============================================================")
    print("   PDW C++ <-> Python HDF5 ROUND-TRIP TEST PASSED!          ")
    print("============================================================")

if __name__ == "__main__":
    main()
