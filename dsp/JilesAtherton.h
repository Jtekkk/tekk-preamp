#pragma once
#include <cmath>
#include <algorithm>

// ============================================================================
//  JilesAtherton : magnetisation M of a ferromagnetic core vs applied field H.
//
//  This is the part that makes iron sound like iron. A memoryless saturation
//  curve gives you the wrong thing; the core has *memory* (the B-H loop), and
//  that lag is the audible "thickness".
//
//  Mean-field model:
//      He    = H + alpha*M                         (effective field)
//      M_an  = Ms * L(He / a),   L(x)=coth(x)-1/x  (anhysteretic, Langevin)
//      M     = (1-c)*M_irr + c*M_an
//      dM_irr/dH = deltaM * (M_an - M_irr)
//                  / (k*delta - alpha*(M_an - M_irr))
//  Using M_irr = (M - c*M_an)/(1-c)  =>  (M_an - M_irr) = (M_an - M)/(1-c),
//  so we track only M. delta = sign(dH/dt); deltaM gates out the unphysical
//  branch (negative susceptibility) when (M_an - M) and dH/dt disagree.
//
//  Closed-form total derivative:
//      dM/dH = [ (1-c)*dMirr/dH + c*dMan/dHe ] / ( 1 - alpha*c*dMan/dHe )
//
//  TIME STEPPING: the ODE is stiff at high drive. We use the implicit
//  trapezoidal rule solved by Newton-Raphson:
//      g(M) = M - Mprev - (dH/2)[ f(M,H) + f(Mprev,Hprev) ] = 0
//  Newton with a numerical dg/dM. 2-4 iterations converge and it stays stable
//  at far lower oversampling than the explicit RK4 the original tape model
//  used -- which is what lets us spend the CPU on the J-A solve instead of on
//  an 8x oversampler.
//
//  Parameters map to physical iron (this is where Jensen/Lundahl/nickel-vs-
//  iron voicing lives):
//      Ms    saturation magnetisation   (overall ceiling)
//      a     anhysteretic loop shape     (knee sharpness)
//      alpha inter-domain coupling       (squareness)
//      k     pinning / coercivity        (loop width = loss = 3rd-harmonic LF)
//      c     reversibility               (0=max hysteresis, 1=anhysteretic)
// ============================================================================
class JilesAtherton
{
public:
    struct Params { double Ms, a, alpha, k, c; };

    void setParams (const Params& p) { P = p; }

    void reset()
    {
        M = 0.0; Hprev = 0.0; Mprev = 0.0; deltaSign = 1.0;
    }

    // advance one sample; H is the applied field for this sample
    inline double process (double H)
    {
        const double dH = H - Hprev;
        if (std::abs (dH) < 1.0e-12)            // no field change -> no motion
        {
            Hprev = H;
            return M;
        }
        deltaSign = (dH > 0.0) ? 1.0 : -1.0;

        const double fPrev = dMdH (Mprev, Hprev, deltaSign);
        double Mn = M;                          // initial guess = last value

        for (int it = 0; it < 8; ++it)          // Newton on trapezoidal residual
        {
            const double fNow = dMdH (Mn, H, deltaSign);
            const double g    = Mn - Mprev - 0.5 * dH * (fNow + fPrev);

            const double h    = 1.0e-6 * std::max (1.0, std::abs (Mn));
            const double fHi  = dMdH (Mn + h, H, deltaSign);
            const double fLo  = dMdH (Mn - h, H, deltaSign);
            const double dgdM = 1.0 - 0.5 * dH * (fHi - fLo) / (2.0 * h);

            const double dMn  = g / dgdM;
            Mn -= dMn;
            if (std::abs (dMn) < 1.0e-9) break;
        }

        Mprev = M; M = Mn; Hprev = H;
        return M;
    }

private:
    // Langevin L(x) = coth(x) - 1/x, with series guard near 0
    static inline double langevin (double x)
    {
        if (std::abs (x) < 1.0e-3) return x / 3.0 - x*x*x / 45.0;
        return 1.0 / std::tanh (x) - 1.0 / x;
    }
    // L'(x) = 1/x^2 - csch^2(x), series guard near 0
    static inline double langevinPrime (double x)
    {
        if (std::abs (x) < 1.0e-3) return 1.0 / 3.0 - x*x / 15.0;
        const double s = std::sinh (x);
        return 1.0 / (x*x) - 1.0 / (s*s);
    }

    inline double dMdH (double Mval, double H, double delta) const
    {
        const double He    = H + P.alpha * Mval;
        const double Man   = P.Ms * langevin (He / P.a);
        const double dManHe= (P.Ms / P.a) * langevinPrime (He / P.a);

        const double diff  = Man - Mval;                  // = (1-c)*(Man-Mirr)
        // deltaM gate: kill unphysical branch
        const double deltaM = ((diff >= 0.0) == (delta > 0.0)) ? 1.0 : 0.0;

        double denom = P.k * delta - P.alpha * diff;
        if (std::abs (denom) < 1.0e-9)                    // pinning denom guard
            denom = (denom < 0.0 ? -1.0 : 1.0) * 1.0e-9;

        const double dMirr = deltaM * diff / denom;       // dM_irr/dH
        const double num   = (1.0 - P.c) * dMirr + P.c * dManHe;
        const double den   = 1.0 - P.alpha * P.c * dManHe;
        return num / den;
    }

    Params P { 1.0, 0.1, 1.0e-3, 0.05, 0.3 };
    double M = 0.0, Hprev = 0.0, Mprev = 0.0, deltaSign = 1.0;
};
