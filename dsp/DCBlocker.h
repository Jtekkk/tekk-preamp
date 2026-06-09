#pragma once
#include <cmath>

// H(z) = (1 - z^-1) / (1 - R z^-1),  R = exp(-2*pi*fc/fs)
// Placed *between* asymmetric stages so the DC offset from one biased stage
// doesn't pump the LF saturation of the next.
class DCBlocker
{
public:
    void prepare (double fs, double fc = 6.0)
    {
        R = std::exp (-2.0 * M_PI * fc / fs);
        reset();
    }
    void reset() { x1 = y1 = 0.0; }

    inline float processSample (float xf)
    {
        const double x = (double) xf;
        const double y = x - x1 + R * y1;
        x1 = x; y1 = y;
        return (float) y;
    }
private:
    double R = 0.999, x1 = 0.0, y1 = 0.0;
};
