#pragma once
#include "JilesAtherton.h"
#include <cmath>

// ============================================================================
//  TransformerStage : the physical reason transformers distort the bass.
//
//  Faraday: V = -N dPhi/dt  =>  Phi = -(1/N) integral V dt.
//  For a sine at fixed level, |Phi| ~ V0/(N*omega): flux amplitude is
//  proportional to 1/omega, so the core is driven DEEPER into saturation as
//  frequency drops. That is why iron thickens lows and leaves highs cleaner.
//
//  We honour this literally:
//      leaky integrator  ->  scale to field H  ->  J-A core  ->  differentiator
//  The integrate/differentiate pair cancels for the linear part (flat small-
//  signal response), but the J-A nonlinearity in the middle sees the 1/omega-
//  weighted flux, so distortion rises toward LF. The leak (R<1) keeps DC
//  bounded and doubles as the core's HPF.
//
//  'fluxDrive' sets how hard the integrated signal pushes the core = how much
//  iron character. Input-vs-output transformers differ mainly in fluxDrive and
//  in the J-A params (output usually voiced heavier in the lows).
// ============================================================================
//  SAMPLE-RATE INDEPENDENCE: the flux is a true time integral, integral V dt,
//  so the integrator input is scaled by dt = 1/fs (written kFsRef/fs against a
//  48 kHz reference). Without it the raw accumulator's gain rises with fs and
//  the core is driven proportionally harder at higher oversampling -- the whole
//  iron character would shift every time you switch 2x/4x/8x. The differentiator
//  divides the same factor back out so the linear in-band gain stays unity and
//  fs-independent (THD, a ratio, is unaffected by that overall scale).
// ============================================================================
class TransformerStage
{
public:
    void setParams (const JilesAtherton::Params& p) { ja.setParams (p); }
    void setFluxDrive (double d) { fluxDrive = d; }
    void setLeakHz    (double hz) { leakHz = hz; }   // transformer LF bandwidth

    void prepare (double fs)
    {
        // integrator leak = the iron's LF bandwidth; also keeps DC bounded
        constexpr double pi = 3.14159265358979323846;   // M_PI isn't standard (MSVC)
        Rint = std::exp (-2.0 * pi * leakHz / fs);
        norm = kFsRef / fs;                 // dt scaling -> fs-independent flux
        ja.reset();
        sInt = 0.0; yPrev = 0.0;
    }
    void reset() { ja.reset(); sInt = 0.0; yPrev = 0.0; }

    inline float processSample (float xf)
    {
        const double x = (double) xf;

        // leaky integrator with timestep scaling -> flux ~ integral V dt
        sInt = Rint * sInt + x * norm;
        const double H = fluxDrive * sInt;

        // nonlinear magnetisation (carries the memory / hysteresis)
        const double M = ja.process (H);

        // differentiate back to a voltage; /norm cancels the integrator's dt so
        // the linear path is unity gain and identical at every sample rate
        const double y = (M - yPrev) / norm;
        yPrev = M;
        return (float) y;
    }

private:
    static constexpr double kFsRef = 48000.0;   // flux reference rate
    JilesAtherton ja;
    double fluxDrive = 1.0, leakHz = 12.0;
    double Rint = 0.999, norm = 1.0, sInt = 0.0, yPrev = 0.0;
};
