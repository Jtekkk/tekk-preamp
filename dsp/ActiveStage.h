#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================================
//  ActiveStage : the swappable gain element.
//
//  This is the seam that makes "clone" and "original" the same plugin.
//   - TekkTanhStage   -> ORIGINAL voicing: biased tanh, harmonics tuned by ear
//                        via the Chebyshev/bias relation. Closed form, cheap.
//   - LutCloneStage   -> CLONE path: a measured static transfer curve (capture
//                        a device with a slow sine sweep, store input->output)
//                        played back through a LUT. Fully implementable today.
//   - (WdfTriodeStage / NeuralStage) -> higher-fidelity clone options, described
//                        in the header comments; they slot in behind this same
//                        interface with zero changes to the chain.
//
//  Every implementation is anti-aliased with 1st-order antiderivative
//  anti-aliasing (ADAA), so generated harmonics that exceed Nyquist are
//  suppressed by the built-in averaging rather than folding back as hash.
// ============================================================================

struct ActiveStage
{
    virtual ~ActiveStage() = default;
    virtual void  prepare (double sampleRate) = 0;
    virtual void  reset()                      = 0;
    virtual float processSample (float x)      = 0;
    virtual const char* name() const           = 0;
};

// ---------------------------------------------------------------------------
//  TEKK voicing : y = tanh(drive*x + bias) - tanh(bias)
//
//  WHY this shape, derived (not guessed):
//    * tanh is the exact large-signal law of a BJT long-tailed pair
//      (I_out = I_ee * tanh(Vin / 2Vt)). Odd-dominant, hard-ish knee.
//    * The bias term breaks symmetry. By the Chebyshev projection of f onto
//      cos(k.theta), an asymmetric f has nonzero EVEN coefficients; the 2nd
//      harmonic level scales with f''(bias). So 'bias' IS the even-harmonic
//      ("tube warmth") control. Subtracting tanh(bias) removes the DC the
//      bias would otherwise inject.
//
//  ADAA1:  y_n = (F1(x_n) - F1(x_{n-1})) / (x_n - x_{n-1})
//          F1(x) = (1/drive) * ln(cosh(drive*x + bias)) - tanh(bias)*x
//          Near x_n == x_{n-1} the quotient is 0/0 -> fall back to the
//          midpoint value of the raw nonlinearity.
// ---------------------------------------------------------------------------
class TekkTanhStage : public ActiveStage
{
public:
    void setDrive (float d) { drive = std::max (1.0e-3f, d); }
    void setBias  (float b) { bias  = b; }

    void prepare (double) override { reset(); }
    void reset() override { x1 = 0.0; f1x1 = antideriv (0.0); }

    inline float processSample (float xf) override
    {
        const double x = (double) xf;
        const double F1 = antideriv (x);
        const double dx = x - x1;

        double y;
        if (std::abs (dx) < 1.0e-7)          // 0/0 guard -> midpoint of raw f
            y = rawShape (0.5 * (x + x1));
        else
            y = (F1 - f1x1) / dx;            // average of f over [x1, x]

        x1 = x; f1x1 = F1;
        return (float) y;
    }

    const char* name() const override { return "TEKK tanh (parametric)"; }

private:
    inline double rawShape (double x) const
    {
        return std::tanh (drive * x + bias) - std::tanh (bias);
    }
    inline double antideriv (double x) const
    {
        // integral of tanh(drive*x+bias) dx = ln(cosh(drive*x+bias))/drive
        return std::log (std::cosh (drive * x + bias)) / drive
             - std::tanh (bias) * x;
    }

    double drive = 1.0, bias = 0.0;
    double x1 = 0.0, f1x1 = 0.0;
};

// ---------------------------------------------------------------------------
//  CLONE via captured transfer curve.
//
//  Capture procedure (real, no ML needed): run a very slow full-range sine
//  (or a staircase) through the target device at the operating gain, record
//  output vs input -> a static transfer function. Load it here as a LUT.
//
//  This reproduces the device's exact harmonic *shape* at a given drive. It
//  does NOT capture memory (transformer hysteresis lives in the J-A cores
//  around it) or bias-dependent dynamics. For those, the higher-fidelity
//  clone options behind this same interface are:
//     WdfTriodeStage  -> Koren triode in a Wave Digital tree (circuit-exact)
//     NeuralStage     -> small LSTM/GRU (RTNeural) trained on device captures
//
//  ADAA here integrates the LUT itself: F1 table = cumulative trapezoid of the
//  curve, then the same difference-quotient as above.
// ---------------------------------------------------------------------------
class LutCloneStage : public ActiveStage
{
public:
    // curve: output samples for inputs uniformly spaced over [-range, +range]
    void loadCurve (const std::vector<float>& curve, float range)
    {
        N = (int) curve.size();
        lo = -range; hi = range; step = (hi - lo) / (N - 1);
        f.assign (curve.begin(), curve.end());

        // build antiderivative table by trapezoidal cumulative integration
        F1.assign (N, 0.0);
        for (int i = 1; i < N; ++i)
            F1[i] = F1[i-1] + 0.5 * (f[i] + f[i-1]) * step;
    }

    void prepare (double) override { reset(); }
    void reset() override { x1 = 0.0; f1x1 = lerpF1 (0.0); }

    inline float processSample (float xf) override
    {
        const double x  = (double) xf;
        const double F  = lerpF1 (x);
        const double dx = x - x1;

        double y;
        if (std::abs (dx) < 1.0e-7)
            y = lerpF (0.5 * (x + x1));
        else
            y = (F - f1x1) / dx;

        x1 = x; f1x1 = F;
        return (float) y;
    }

    const char* name() const override { return "Clone (captured curve LUT)"; }

private:
    inline double idxClamped (double x) const
    {
        double p = (x - lo) / step;
        return std::clamp (p, 0.0, (double) (N - 1));
    }
    inline double lerpF (double x) const
    {
        double p = idxClamped (x); int i = (int) p; double t = p - i;
        if (i >= N - 1) return f[N-1];
        return f[i] * (1.0 - t) + f[i+1] * t;
    }
    inline double lerpF1 (double x) const
    {
        double p = idxClamped (x); int i = (int) p; double t = p - i;
        if (i >= N - 1) return F1[N-1];
        return F1[i] * (1.0 - t) + F1[i+1] * t;
    }

    int N = 0; double lo = -1, hi = 1, step = 1;
    std::vector<double> f, F1;
    double x1 = 0.0, f1x1 = 0.0;
};
