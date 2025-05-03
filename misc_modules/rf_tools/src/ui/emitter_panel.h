#pragma once
#include "../pri/emitter_tracker.h"
#include <vector>

namespace rf_tools {

class EmitterPanel {
public:
    EmitterPanel();
    ~EmitterPanel() = default;

    void draw(EmitterTracker& tracker);

private:
    float ui_freq_tol_khz_ = 50.0f;
    float ui_pw_tol_us_ = 1.5f;
    float ui_doa_tol_deg_ = 15.0f;
    float ui_timeout_sec_ = 5.0f;

    int selected_emitter_id_ = -1;
};

} // namespace rf_tools
