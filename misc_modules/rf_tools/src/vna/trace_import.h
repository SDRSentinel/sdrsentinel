#pragma once
#include <string>
#include <vector>
#include "vna_analyzer.h"

namespace rf_tools {
namespace vna {

struct TraceImportResult {
    bool ok = false;
    std::string error;                // set when ok == false
    std::string format;               // "Touchstone .s2p" or "CSV"
    std::vector<VNASweepPoint> trace; // S21 as dB and degrees, ascending frequency
};

// Touchstone v1 two-port data (.s2p): option line "# <unit> S <RI|MA|DB> R <z>", '!' comments, records
// wrapped over several lines. Per record: freq S11 S21 S12 S22 (each a pair), S21 is the second pair.
TraceImportResult parseTouchstoneS2P(const std::string& text);

// freq_hz, s21_db[, phase_deg]; comma, semicolon, tab or space separated; header row optional.
TraceImportResult parseTraceCsv(const std::string& text);

// Chooses the parser from the file extension. .s1p is rejected (reflection, not transmission).
// Never returns any data other than what the file contains.
TraceImportResult loadTraceFile(const std::string& path);

// Writes freq_hz, s21_db, phase_deg (empty phase when not measured). Returns an error text, empty on success.
std::string writeTraceCsv(const std::string& path, const std::vector<VNASweepPoint>& trace);

} // namespace vna
} // namespace rf_tools
