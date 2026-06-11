#pragma once
#include "JilesAtherton.h"
#include <cmath>
#include <algorithm>

// ============================================================================
//  TransformerStage : the physical reason transformers distort the bass.
//
//  Faraday: V = -N dPhi/dt  =>  Phi = -(1/N) integral V dt.
//  For a sine at fixed level, |Phi| ~ V0/(N*omega): flux amplitude is
//  proportional to 1/omega, so the core is driven DEEPER into saturation as
//  frequency drops. That is why iron thickens lows and leaves highs cleaner.
//
//  We honour this literally:
//      leaky integrator -> scale to field H -> J-A core -> differentiator
//  and then NORMALISE: the linear in-band gain of that chain is chi*fluxDrive
//  (chi = the core's small-signal susceptibility dM/dH), NOT unity -- with the
//  voiced fluxDrive values that is ~ -61 dB per transformer, i.e. silence.
//  Dividing the output by chi*fluxDrive makes the stage unity gain at small
//  signal, so 'fluxDrive' becomes a pure "iron amount" control: more drive =
//  the core saturates earlier/deeper (compression + harmonics rise), while
//  quiet material passes at unity. THD and the 1/omega character are ratios,
//  so the voicing measurements are unchanged by this scaling.
//
//  chi is MEASURED on the actual model (probe a settled small minor loop and
//  project M onto H) rather than derived: with hysteresis the effective
//  susceptibility is not the anhysteretic slope -- the deltaM gate freezes
//  M_irr over parts of the cycle. The J-A step depends only on dH (hysteresis
//  is rate-independent), so a fixed points-per-cycle probe gives the same chi
//  at every sample rate.
//
//  'fluxDrive' sets how hard the integrated signal pushes the core = how much
//  iron character. Input-vs-output transformers differ mainly in fluxDrive and
//  in the J-A params (output usually voiced heavier in the lows). fluxDrive ~ 0
//  means "no iron": the stage passes dry (and the normalisation's small-drive
//  limit IS dry, so the knob is continuous down to zero).
// ============================================================================
//  SAMPLE-RATE INDEPENDENCE: the flux is a true time integral, integral V dt,
//  so the integrator input is scaled by dt = 1/fs (written kFsRef/fs against a
//  48 kHz reference). Without it the raw accumulator's gain rises with fs and
//  the core is driven proportionally harder at higher oversampling -- the whole
//  iron character would shift every time you switch 2x/4x/8x. The differentiator
//  divides the same factor back out.
// ============================================================================
class TransformerStage
{
public:
    void setParams (const JilesAtherton::Params& p)
    {
        ja.setParams (p);
        chi = measureChi (p);
        updateMakeup();
    }
    void setFluxDrive (double d) { fluxDrive = d; updateMakeup(); }
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
        if (bypassed) return xf;            // fluxDrive ~ 0 -> no iron, pass dry

        const double x = (double) xf;

        // leaky integrator with timestep scaling -> flux ~ integral V dt
        sInt = Rint * sInt + x * norm;
        const double H = fluxDrive * sInt;

        // nonlinear magnetisation (carries the memory / hysteresis)
        const double M = ja.process (H);

        // differentiate back to a voltage; /norm cancels the integrator's dt,
        // makeup (= 1/(chi*fluxDrive)) cancels the linear gain of the core so
        // the small-signal path is unity at every sample rate and drive
        const double y = (M - yPrev) * (makeup / norm);
        yPrev = M;
        return (float) y;
    }

private:
    // small-signal susceptibility of the J-A core around the demagnetised
    // origin, measured over a settled minor loop (projection of M onto H
    // rejects any DC offset in M; the first cycle is skipped as transient)
    static double measureChi (const JilesAtherton::Params& p)
    {
        constexpr double pi = 3.14159265358979323846;
        JilesAtherton probe; probe.setParams (p); probe.reset();
        const int ppc = 256, cycles = 3;
        const double h = 0.01 * p.a;        // well inside the linear region
        double shh = 0.0, shm = 0.0;
        for (int n = 0; n < ppc * cycles; ++n)
        {
            const double H = h * std::sin (2.0 * pi * n / ppc);
            const double M = probe.process (H);
            if (n >= ppc) { shh += H * H; shm += H * M; }
        }
        return std::max (shm / std::max (shh, 1.0e-300), 1.0e-6);
    }

    void updateMakeup()
    {
        bypassed = (chi * fluxDrive < 1.0e-9);
        makeup = bypassed ? 1.0 : 1.0 / (chi * fluxDrive);
    }

    static constexpr double kFsRef = 48000.0;   // flux reference rate
    JilesAtherton ja;
    double fluxDrive = 1.0, leakHz = 12.0;
    double chi = 1.0, makeup = 1.0;
    bool   bypassed = false;
    double Rint = 0.999, norm = 1.0, sInt = 0.0, yPrev = 0.0;
};
