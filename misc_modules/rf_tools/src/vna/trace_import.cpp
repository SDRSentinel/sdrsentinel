#include "trace_import.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rf_tools {
namespace vna {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kMinDb = -200.0f;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) { a++; }
    while (b > a && std::isspace((unsigned char)s[b - 1])) { b--; }
    return s.substr(a, b - a);
}

// Whole token must be a number
bool parseNumber(const std::string& tok, double& out) {
    if (tok.empty()) { return false; }
    char* end = nullptr;
    out = std::strtod(tok.c_str(), &end);
    return end && *end == '\0';
}

std::vector<std::string> splitTokens(const std::string& line, const std::string& delims) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (delims.find(c) != std::string::npos) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        }
        else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) { out.push_back(cur); }
    return out;
}

// Keeps empty fields when a ',', ';' or tab delimiter is present, otherwise splits on whitespace
std::vector<std::string> splitCsvFields(const std::string& line) {
    for (char d : { ',', ';', '\t' }) {
        if (line.find(d) == std::string::npos) { continue; }
        std::vector<std::string> out;
        std::string cur;
        for (char c : line) {
            if (c == d) { out.push_back(trim(cur)); cur.clear(); }
            else { cur.push_back(c); }
        }
        out.push_back(trim(cur));
        return out;
    }
    return splitTokens(line, " ");
}

float linToDb(double mag) {
    if (!(mag > 1e-10)) { return kMinDb; }
    return (float)(20.0 * std::log10(mag));
}

TraceImportResult fail(const std::string& format, const std::string& msg) {
    TraceImportResult r;
    r.format = format;
    r.error = msg;
    return r;
}

bool strictlyAscending(const std::vector<VNASweepPoint>& t, size_t& badIdx) {
    for (size_t i = 1; i < t.size(); ++i) {
        if (!(t[i].freqHz > t[i - 1].freqHz)) { badIdx = i; return false; }
    }
    return true;
}

} // namespace

TraceImportResult parseTouchstoneS2P(const std::string& text) {
    const std::string fmtName = "Touchstone .s2p";

    // Defaults of the specification when the option line is missing or short
    double unitScale = 1.0e9;
    std::string format = "ma";
    bool haveOptions = false;

    std::vector<double> numbers;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        lineNo++;
        size_t bang = line.find('!');
        if (bang != std::string::npos) { line.erase(bang); }
        line = trim(line);
        if (line.empty()) { continue; }

        if (line[0] == '[') {
            return fail(fmtName, "Touchstone 2.0 keyword '" + line + "' (line " + std::to_string(lineNo) + "): only Touchstone 1.x files are supported");
        }

        if (line[0] == '#') {
            if (haveOptions) { continue; } // only the first option line counts
            haveOptions = true;
            auto toks = splitTokens(lower(line.substr(1)), " \t");
            for (size_t i = 0; i < toks.size(); ++i) {
                const std::string& t = toks[i];
                if (t == "hz") { unitScale = 1.0; }
                else if (t == "khz") { unitScale = 1.0e3; }
                else if (t == "mhz") { unitScale = 1.0e6; }
                else if (t == "ghz") { unitScale = 1.0e9; }
                else if (t == "ri" || t == "ma" || t == "db") { format = t; }
                else if (t == "s") {}
                else if (t == "y" || t == "z" || t == "h" || t == "g") {
                    return fail(fmtName, "Option line has parameter type '" + t + "', only S-parameters are supported");
                }
                else if (t == "r") { i++; } // reference impedance does not change S21 as a ratio
                else { return fail(fmtName, "Unrecognized token '" + t + "' in option line (line " + std::to_string(lineNo) + ")"); }
            }
            continue;
        }

        for (const auto& tok : splitTokens(line, " \t,")) {
            double v;
            if (!parseNumber(tok, v) || !std::isfinite(v)) {
                return fail(fmtName, "Bad number '" + tok + "' on line " + std::to_string(lineNo));
            }
            numbers.push_back(v);
        }
    }

    if (numbers.empty()) { return fail(fmtName, "No data found"); }
    // 2-port record: frequency + 4 complex parameters. Noise parameter blocks (5 numbers per record) are not supported.
    if (numbers.size() % 9 != 0) {
        return fail(fmtName, "Data has " + std::to_string(numbers.size()) + " numbers, not a multiple of 9 (frequency + S11 S21 S12 S22): not a plain 2-port file");
    }

    TraceImportResult r;
    r.format = fmtName;
    for (size_t k = 0; k + 8 < numbers.size(); k += 9) {
        double f = numbers[k] * unitScale;
        double a = numbers[k + 3]; // S21 = second pair (v1 order S11 S21 S12 S22)
        double b = numbers[k + 4];
        VNASweepPoint pt;
        pt.freqHz = f;
        if (format == "ri") {
            pt.magnitudeDb = linToDb(std::hypot(a, b));
            pt.phaseDeg = (float)(std::atan2(b, a) * 180.0 / kPi);
        }
        else if (format == "ma") {
            pt.magnitudeDb = linToDb(std::fabs(a));
            pt.phaseDeg = (float)b;
        }
        else { // db
            pt.magnitudeDb = (float)a;
            pt.phaseDeg = (float)b;
        }
        r.trace.push_back(pt);
    }

    if (r.trace.size() < 2) { return fail(fmtName, "Need at least 2 frequency points"); }
    size_t bad = 0;
    if (!strictlyAscending(r.trace, bad)) {
        return fail(fmtName, "Frequencies are not strictly ascending at record " + std::to_string(bad + 1));
    }
    r.ok = true;
    return r;
}

TraceImportResult parseTraceCsv(const std::string& text) {
    const std::string fmtName = "CSV";
    TraceImportResult r;
    r.format = fmtName;

    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    bool firstRow = true;
    size_t columns = 0;
    while (std::getline(in, line)) {
        lineNo++;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == '!') { continue; }

        auto toks = splitCsvFields(line);
        double f;
        if (firstRow) {
            firstRow = false;
            if (!parseNumber(toks[0], f)) {
                // Header row; the frequency must be in Hz because values are not rescaled
                std::string h = lower(toks[0]);
                if (h.find("mhz") != std::string::npos || h.find("ghz") != std::string::npos || h.find("khz") != std::string::npos) {
                    return fail(fmtName, "Frequency column '" + toks[0] + "' must be in Hz (freq_hz)");
                }
                continue;
            }
        }

        if (toks.size() < 2 || toks.size() > 3) {
            return fail(fmtName, "Line " + std::to_string(lineNo) + ": expected freq_hz, s21_db[, phase_deg]");
        }
        if (columns == 0) { columns = toks.size(); }
        if (toks.size() != columns) {
            return fail(fmtName, "Line " + std::to_string(lineNo) + ": column count differs from the first data row");
        }

        double db, ph = NAN;
        if (!parseNumber(toks[0], f) || !std::isfinite(f) || f <= 0.0) {
            return fail(fmtName, "Line " + std::to_string(lineNo) + ": bad frequency '" + toks[0] + "'");
        }
        if (!parseNumber(toks[1], db) || !std::isfinite(db)) {
            return fail(fmtName, "Line " + std::to_string(lineNo) + ": bad S21 magnitude '" + toks[1] + "'");
        }
        if (toks.size() == 3 && !toks[2].empty() && !parseNumber(toks[2], ph)) {
            return fail(fmtName, "Line " + std::to_string(lineNo) + ": bad phase '" + toks[2] + "'");
        }

        VNASweepPoint pt;
        pt.freqHz = f;
        pt.magnitudeDb = (float)db;
        pt.phaseDeg = (float)ph;
        r.trace.push_back(pt);
    }

    if (r.trace.size() < 2) { return fail(fmtName, "Need at least 2 data rows"); }
    size_t bad = 0;
    if (!strictlyAscending(r.trace, bad)) {
        return fail(fmtName, "Frequencies are not strictly ascending at data row " + std::to_string(bad + 1));
    }
    r.ok = true;
    return r;
}

TraceImportResult loadTraceFile(const std::string& path) {
    std::string p = trim(path);
    if (p.empty()) { return fail("", "Enter a file path first"); }

    std::string ext;
    size_t dot = p.find_last_of('.');
    if (dot != std::string::npos) { ext = lower(p.substr(dot + 1)); }

    if (ext == "s1p") {
        return fail("Touchstone .s1p", ".s1p holds one-port reflection data (S11), not transmission. Load an .s2p file containing S21");
    }
    if (ext.size() == 3 && ext[0] == 's' && ext[2] == 'p' && ext != "s2p") {
        return fail("Touchstone ." + ext, "Only two-port .s2p files are supported");
    }
    if (ext != "s2p" && ext != "csv" && ext != "txt" && ext != "dat") {
        return fail("", "Unsupported file type '." + ext + "': use .s2p or .csv");
    }

    std::ifstream f(p, std::ios::binary);
    if (!f) { return fail("", "Cannot open '" + p + "'"); }
    std::stringstream ss;
    ss << f.rdbuf();

    return (ext == "s2p") ? parseTouchstoneS2P(ss.str()) : parseTraceCsv(ss.str());
}

std::string writeTraceCsv(const std::string& path, const std::vector<VNASweepPoint>& trace) {
    if (trace.empty()) { return "No trace to export"; }
    std::ofstream f(path);
    if (!f) { return "Cannot write '" + path + "'"; }
    f << "freq_hz,s21_db,phase_deg\n";
    char buf[128];
    for (const auto& pt : trace) {
        if (std::isfinite(pt.phaseDeg)) {
            std::snprintf(buf, sizeof(buf), "%.3f,%.6f,%.6f\n", pt.freqHz, pt.magnitudeDb, pt.phaseDeg);
        }
        else {
            std::snprintf(buf, sizeof(buf), "%.3f,%.6f,\n", pt.freqHz, pt.magnitudeDb);
        }
        f << buf;
    }
    f.flush();
    if (!f) { return "Write to '" + path + "' failed"; }
    return "";
}

} // namespace vna
} // namespace rf_tools
