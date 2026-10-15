#!/usr/bin/env python3
"""
SDRSentinel rf_tools PDW C++ to Python Round-Trip and HDF5 Integrity Test
Validates binary serialization and the gr-pdw style HDF5 layout (see read_sdrpp_pdw.py for the differences).
"""

import sys
import os
import math
import argparse

try:
    import numpy as np
    import h5py
except ImportError as e:
    print(f"SKIP: missing Python dependency ({e.name}); install numpy and h5py to run this test.")
    sys.exit(0)

# Import reader from tools
tools_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, tools_dir)
from read_sdrpp_pdw import read_pdw_file, convert_to_hdf5

def main():
    parser = argparse.ArgumentParser(description="PDW C++ -> Python round-trip test")
    parser.add_argument("pdwbin", nargs="?", default="test_output/pdw_test.pdwbin",
                        help="path to the .pdwbin written by test_root_dev_integration (default: %(default)s)")
    args = parser.parse_args()

    print("============================================================")
    print("   rf_tools PDW Binary C++ -> Python Round-Trip Test        ")
    print("============================================================")

    pdwbin_path = args.pdwbin
    h5_path = os.path.splitext(pdwbin_path)[0] + ".h5"

    if not os.path.exists(pdwbin_path):
        sys.stderr.write(f"ERROR: {pdwbin_path} does not exist. Run test_root_dev_integration first "
                         "(it writes test_output/pdw_test.pdwbin relative to its working directory) "
                         "or pass the file path as an argument.\n")
        sys.exit(1)

    print(f"[TEST] Reading binary PDW stream from '{pdwbin_path}'...")
    data = read_pdw_file(pdwbin_path)

    meta = data["metadata"]
    records = data["records"]

    # 1. Validate Header
    fs = 2.4e6
    time_base = 1700000000.25
    assert meta["version"] == 2, f"Header version mismatch: {meta['version']}"
    assert math.isclose(meta["samp_rate"], fs, rel_tol=1e-9), f"Sample rate mismatch: {meta['samp_rate']}"
    assert meta["time_base_unix"] == time_base, f"Time base mismatch: {meta['time_base_unix']}"
    assert math.isclose(meta["center_freq"], 433.92e6, rel_tol=1e-5), f"Center freq mismatch: {meta['center_freq']}"
    assert math.isclose(meta["ref_level"], -25.0, rel_tol=1e-4), f"Ref level mismatch: {meta['ref_level']}"
    print("  -> Header metadata asserted: 2.4 MSPS, time base, 433.92 MHz, Ref -25 dBm: MATCH")

    # 2. Validate Records count and content
    assert len(records) == 10, f"Expected 10 records, got {len(records)}"
    for i, r in enumerate(records, start=1):
        assert r["pulse_index"] == i, f"Pulse index mismatch at {i}: {r['pulse_index']}"
        assert r["sample_index"] == i * 2000, f"Sample index mismatch at {i}"
        expected_toa = (i * 2000) / fs
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

    # 4. Read back HDF5 and verify the gr-pdw style layout
    print(f"[TEST] Verifying HDF5 schema and dataset integrity...")
    gr_pdw_datasets = ["pulse_width_samps", "pulse_width_secs", "pulse_power", "noise_power",
                       "freq_start", "toa_course", "toa_fine"]
    with h5py.File(h5_path, "r") as hf:
        for attr in ("time_unix", "time_py", "samp_rate", "ref_level"):
            assert attr in hf.attrs, f"Missing attribute {attr}"
        assert "center_freq" not in hf.attrs
        assert math.isclose(hf.attrs["samp_rate"], fs, rel_tol=1e-9)
        assert math.isclose(hf.attrs["ref_level"], -25.0, rel_tol=1e-4)
        assert hf.attrs["time_unix"] == time_base
        assert hf.attrs["time_py"] == "2023-11-14T22:13:20Z", hf.attrs["time_py"]

        top = [k for k in hf.keys() if k != "sdrpp"]
        # Dataset names define the layout; HDF5 link iteration order is not part of it.
        assert set(top) == set(gr_pdw_datasets), f"Dataset names mismatch: {top}"
        for ds_name in gr_pdw_datasets:
            ds = hf[ds_name]
            assert ds.dtype == np.float64, f"{ds_name} dtype {ds.dtype}"
            assert ds.maxshape == (None,), f"{ds_name} maxshape {ds.maxshape}"
            assert ds.chunks is not None, f"{ds_name} is not chunked"
            assert len(ds[:]) == 10, f"Dataset {ds_name} length {len(ds[:])} != 10"
        for ds_name in ("pulse_power", "noise_power", "toa_course", "toa_fine"):
            assert "definition" in hf[ds_name].attrs, f"{ds_name} lacks a definition attribute"

        np.testing.assert_array_equal(hf["pulse_width_samps"][:], np.full(10, 20.0))
        np.testing.assert_allclose(hf["pulse_width_secs"][:], 10.0e-6, rtol=1e-6)
        np.testing.assert_allclose(hf["pulse_power"][:], -40.0, rtol=1e-4)
        np.testing.assert_allclose(hf["noise_power"][:], -95.0, rtol=1e-4)

        # Absolute time reconstruction from time base + relative TOA
        assert "sdrpp" in hf, "Missing sdrpp group"
        g = hf["sdrpp"]
        for name in ("pulse_index", "sample_index", "toa", "absolute_freq", "average_power", "snr", "pri", "doa_azimuth"):
            assert name in g and g[name].dtype == np.float64 and g[name].maxshape == (None,), f"sdrpp/{name}"
        assert math.isclose(g.attrs["center_freq"], 433.92e6, rel_tol=1e-5)
        np.testing.assert_allclose(g["pri"][:], 0.001, rtol=1e-6)
        abs_toa = hf.attrs["time_unix"] + g["toa"][:]
        expected = time_base + np.arange(1, 11) * 2000 / fs
        np.testing.assert_allclose(abs_toa, expected, rtol=0, atol=1e-9)
        np.testing.assert_array_equal(hf["toa_course"][:], np.full(10, math.floor(time_base)))

    print("  -> HDF5 gr-pdw style datasets (7 fields x 10 records) and sdrpp group verified against C++ ground truth: MATCH")
    print("============================================================")
    print("   PDW C++ <-> Python HDF5 ROUND-TRIP TEST PASSED!          ")
    print("============================================================")

if __name__ == "__main__":
    main()
