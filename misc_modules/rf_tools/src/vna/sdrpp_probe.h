#pragma once
#include <memory>
#include <string>
#include "scalar_sweep.h"

namespace rf_tools {
namespace vna {

// Receiver side of the scalar sweep: its own narrow VFO placed offsetHz above the source center, so the
// DC spike / LO leakage stays out of the measurement. The PDW detector VFO is never touched.
// Create and destroy on the UI thread (VFO creation/removal); tune/measure/finish run on the sweep thread.
class SdrppScalarProbe : public ScalarProbe {
public:
    // Fails (nullptr, err set) when the SDR++ source is not running or the offset does not fit the source bandwidth
    static std::unique_ptr<SdrppScalarProbe> create(const std::string& moduleName, const ScalarSweepSettings& s, std::string& err);
    ~SdrppScalarProbe() override;

    bool tune(double hz) override;
    bool measurePowerDb(double settleSec, double dwellSec, float& outDb) override;
    void requestCancel() override;
    void finish() override;
    std::string lastError() const override { return error_; }

private:
    SdrppScalarProbe() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string error_;
};

} // namespace vna
} // namespace rf_tools
