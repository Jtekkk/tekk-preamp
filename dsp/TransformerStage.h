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
class TransformerStage
{
public:
    void setParams (const JilesAtherton::Params& p) { ja.setParams (p); }
    void setFluxDrive (double d) { fluxDrive = d; }

    void prepare (double fs)
    {
        // integrator leak ~ a few Hz so the linear path stays flat in-band
        Rint = std::exp (-2.0 * M_PI * 4.0 / fs);
        ja.reset();
        sInt = 0.0; yPrev = 0.0;
    }
    void reset() { ja.reset(); sInt = 0.0; yPrev = 0.0; }

    inline float processSample (float xf)
    {
        const double x = (double) xf;

        // leaky integrator -> quantity proportional to flux/applied field
        sInt = Rint * sInt + x;
        const double H = fluxDrive * sInt;

        // nonlinear magnetisation (carries the memory / hysteresis)
        const double M = ja.process (H);

        // differentiate back to a voltage; linear part of (int->diff) ~ unity
        const double y = M - yPrev;
        yPrev = M;
        return (float) y;
    }

private:
    JilesAtherton ja;
    double fluxDrive = 1.0;
    double Rint = 0.999, sInt = 0.0, yPrev = 0.0;
};
