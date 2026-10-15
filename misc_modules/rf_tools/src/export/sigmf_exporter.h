#pragma once
#include "../pdw/pdw.h"
#include "../pri/emitter_tracker.h"
#include <vector>
#include <string>

namespace rf_tools {

enum class ExportFormat {
    CSV,
    JSON,
    SIGMF
};

class SigMFExporter {
public:
    static bool exportSigMF(const std::string& baseFilePath,
                            const std::vector<PDW>& pdws,
                            double sampleRate,
                            double centerFreqHz,
                            const std::string& author = "Suleyman Poyraz",
                            const std::string& description = "SDRSentinel rf_tools pulse extraction and direction finding recording");

    static bool exportJSON(const std::string& filePath,
                           const std::vector<PDW>& pdws,
                           const std::vector<TrackedEmitter>& emitters,
                           double sampleRate,
                           double centerFreqHz);

    static bool exportCSV(const std::string& filePath,
                          const std::vector<PDW>& pdws);

    static std::string getIso8601Utc();
};

} // namespace rf_tools
