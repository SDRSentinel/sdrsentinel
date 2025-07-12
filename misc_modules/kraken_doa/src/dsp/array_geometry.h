#pragma once
#include <vector>

namespace kraken_doa::dsp {

    constexpr double SPEED_OF_LIGHT = 299792458.0;

    enum class ArrayType {
        UCA,
        ULA
    };

    struct ArrayGeometry {
        ArrayType type = ArrayType::UCA;
        int elements = 5;
        double sizeMeters = 0.5; // UCA radius or ULA spacing

        // Element positions in wavelengths at frequency freqHz (lambda = c / f)
        // ULA: x_m = 0, y_m = -m d.  UCA: x_m = r cos(2 pi m / M), y_m = -r sin(2 pi m / M).
        void positions(double freqHz, std::vector<double>& x, std::vector<double>& y) const;
    };

}
