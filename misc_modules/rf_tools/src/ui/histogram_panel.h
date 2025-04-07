#pragma once
#include "../pdw/pdw.h"
#include "../pri/pri_analyzer.h"
#include <vector>
#include <deque>
#include <mutex>

namespace rf_tools {
    class HistogramPanel {
    public:
        HistogramPanel();
        ~HistogramPanel();

        void addPDW(const PDW& pdw);
        void clear();

        void draw(const PRIAnalyzer& priAnalyzer);

    private:
        void updateBinsLocked();

        mutable std::mutex _mutex;
        static constexpr size_t MAX_HISTORY = 512;
        std::deque<PDW> _pdwHistory;

        int _binCount = 32;

        std::vector<float> _priBins;
        float _priMinUs = 0.0f;
        float _priMaxUs = 1000.0f;

        std::vector<float> _pwBins;
        float _pwMinUs = 0.0f;
        float _pwMaxUs = 50.0f;

        std::vector<float> _freqBins;
        float _freqMinKhz = -1000.0f;
        float _freqMaxKhz = 1000.0f;
    };
}
