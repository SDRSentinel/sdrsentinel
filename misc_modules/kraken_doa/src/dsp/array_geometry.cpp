#include "array_geometry.h"
#include <cmath>

namespace kraken_doa::dsp {

    void ArrayGeometry::positions(double freqHz, std::vector<double>& x, std::vector<double>& y) const {
        x.assign(elements, 0.0);
        y.assign(elements, 0.0);
        const double size = sizeMeters * freqHz / SPEED_OF_LIGHT;
        for (int m = 0; m < elements; m++) {
            if (type == ArrayType::ULA) {
                x[m] = 0.0;
                y[m] = -m * size;
            }
            else {
                const double a = 2.0 * M_PI * m / elements;
                x[m] = size * std::cos(a);
                y[m] = -size * std::sin(a);
            }
        }
    }

}
