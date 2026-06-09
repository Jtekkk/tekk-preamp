#pragma once
#include "ActiveStage.h"
#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================================
//  TriodeStage : a "tube" voicing -- the higher-fidelity option the README
//  flags behind the ActiveStage seam, here as a Koren triode on a plate
//  LOAD LINE rather than a full Wave Digital tree.
//
//  WHY this is real (not a guessed curve):
//    * Norman Koren's triode law gives plate current Ip(Vgk, Vpk) for a real
//      tube (12AX7-ish defaults). Driving it through a common-cathode stage
//      (B+ supply, plate resistor Ra, bypassed cathode bias Rk) and solving the
//      load line Vpk = B+ - Ip*Ra at each grid voltage yields the genuine
//      grid->plate transfer: gentle near bias, COMPRESSING into cutoff one way
//      and into plate saturation the other. That asymmetry is what makes a
//      triode EVEN-harmonic dominant (H2 > H3) -- the opposite of the BJT
//      long-tailed pair (TekkTanhStage), which is odd-dominant. Measured in the
//      harness.
//
//  We solve the (static) transfer once into a curve, then play it back through
//  the same LUT + 1st-order ADAA as the captured-clone path -- so anti-aliasing
//  is the exact, already-validated code. Reactive/dynamic memory is NOT modelled
//  here (the transformer J-A cores around this stage carry the chain's memory).
//
//  drive / bias match TekkTanhStage so the same two knobs voice all characters:
//    drive = grid drive (input gain into the tube),
//    bias  = grid operating-point offset (shifts symmetry -> even-harmonic dial).
// ============================================================================
class TriodeStage : public ActiveStage
{
public:
    struct Tube    { double mu = 100.0, Ex = 1.4, Kg1 = 1060.0, Kp = 600.0, Kvb = 300.0; };
    struct Circuit { double B = 300.0, Ra = 100000.0, Rk = 1500.0; };

    TriodeStage() { buildCurve(); }

    void setDrive (float d) { drive = std::max (1.0e-3f, d); }
    void setBias  (float b) { bias  = b; }
    void setTube  (const Tube& t, const Circuit& c) { tube = t; ckt = c; buildCurve(); }

    void prepare (double fs) override { lut.prepare (fs); }
    void reset()             override { lut.reset(); }

    inline float processSample (float x) override
    {
        return lut.processSample ((float) (drive * (double) x + bias));
    }

    const char* name() const override { return "Tube (Koren triode)"; }

private:
    static double koren (double Vgk, double Vpk, const Tube& t)
    {
        const double Vp = Vpk > 0.0 ? Vpk : 0.0;
        const double E1 = (Vp / t.Kp)
            * std::log (1.0 + std::exp (t.Kp * (1.0 / t.mu + Vgk / std::sqrt (t.Kvb + Vp*Vp))));
        return E1 > 0.0 ? std::pow (E1, t.Ex) / t.Kg1 : 0.0;
    }

    double solveIpQ() const                         // quiescent plate current
    {
        double Ip = 0.5e-3;
        for (int i = 0; i < 200; ++i)               // damped fixed point (~40 to converge)
        {
            const double Vgk = -Ip * ckt.Rk, Vpk = ckt.B - Ip * ckt.Ra;
            const double step = 0.5 * (koren (Vgk, Vpk, tube) - Ip);
            Ip += step;
            if (std::abs (step) < 1.0e-12) break;
        }
        return Ip;
    }
    double solveVpk (double Vgk) const              // plate voltage on the load line
    {
        double Vpk = 0.5 * ckt.B;
        for (int i = 0; i < 60; ++i)                // Newton (~8 to converge)
        {
            const double f  = ckt.B - koren (Vgk, Vpk, tube) * ckt.Ra - Vpk;
            const double h  = 0.05;
            const double df = -(koren (Vgk, Vpk + h, tube) - koren (Vgk, Vpk - h, tube))
                              / (2.0 * h) * ckt.Ra - 1.0;
            const double d  = f / df;
            Vpk = std::min (std::max (Vpk - d, 0.0), ckt.B);
            if (std::abs (d) < 1.0e-7) break;
        }
        return Vpk;
    }

    void buildCurve()
    {
        const double Ipq = solveIpQ();
        const double Vkq = Ipq * ckt.Rk, Vpkq = ckt.B - Ipq * ckt.Ra;
        auto plate = [&] (double u) { return -(solveVpk (vpu * u - Vkq) - Vpkq); };

        const double slope = (plate (0.01) - plate (-0.01)) / 0.02;   // unit-gain norm
        std::vector<float> curve ((size_t) N);
        for (int i = 0; i < N; ++i)
        {
            const double u = -R + 2.0 * R * i / (N - 1);
            curve[(size_t) i] = (float) (plate (u) / slope);
        }
        lut.loadCurve (curve, (float) R);
    }

    LutCloneStage lut;
    Tube    tube;
    Circuit ckt;
    double  drive = 1.0, bias = 0.0;
    static constexpr int    N   = 2048;     // transfer-curve resolution
    static constexpr double R   = 6.0;      // input domain fed to the LUT
    static constexpr double vpu = 0.45;     // signal-units -> grid volts
};
