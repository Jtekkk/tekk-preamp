#include "../dsp/ActiveStage.h"
#include "../dsp/TransformerStage.h"
#include "../dsp/TriodeStage.h"
#include "SimpleFFT.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <functional>

static double db (double lin) { return 20.0 * std::log10 (std::max (lin, 1.0e-300)); }

// Generate a bin-centred sine: freq = binIndex * fs / N  -> lands exactly on a
// bin, so a rectangular window leaks nothing and harmonics sit on exact bins.
static int binCentredTone (std::vector<double>& buf, double fs, double approxHz,
                           double amp, int N, bool oddBin = false)
{
    int bin = (int) std::round (approxHz * N / fs);
    if (oddBin && (bin % 2 == 0)) bin += 1;     // avoid fs/2^k degeneracy where
                                                // aliases fold onto harmonic bins
    double f = bin * fs / (double) N;
    buf.resize (N);
    for (int n = 0; n < N; ++n) buf[n] = amp * std::sin (2.0 * M_PI * f * n / fs);
    return bin;
}

// THD = sqrt(sum harmonics^2) / fundamental, using exact harmonic bins.
static double thd (const std::vector<double>& mag, int fundBin, int nHarm)
{
    double fund = mag[fundBin];
    double s = 0.0;
    for (int h = 2; h <= nHarm; ++h)
    {
        int b = fundBin * h;
        if (b < (int) mag.size()) s += mag[b] * mag[b];
    }
    return std::sqrt (s) / std::max (fund, 1.0e-300);
}

// Energy that is NOT on a harmonic bin = aliasing + noise floor.
static double aliasFloor (const std::vector<double>& mag, int fundBin, int nHarm)
{
    std::vector<char> isHarm (mag.size(), 0);
    for (int h = 1; h <= nHarm; ++h)
        if (fundBin * h < (int) mag.size()) isHarm[fundBin * h] = 1;
    isHarm[0] = 1;                                  // ignore DC bin
    double s = 0.0;
    for (size_t b = 1; b < mag.size(); ++b)
        if (! isHarm[b]) s += mag[b] * mag[b];
    return std::sqrt (s);
}

static std::vector<double> runStage (std::function<float(float)> f,
                                     const std::vector<double>& in)
{
    std::vector<double> out (in.size());
    for (size_t n = 0; n < in.size(); ++n) out[n] = f ((float) in[n]);
    return out;
}

int main()
{
    const int N = 1 << 16;          // 65536

    printf ("================================================================\n");
    printf ("  PREAMP DSP CORE -- offline validation harness\n");
    printf ("================================================================\n\n");

    // ----------------------------------------------------------------------
    //  EXPERIMENT 1 -- bias controls EVEN harmonics (Chebyshev claim)
    //  Fixed drive, sweep bias, read harmonic ladder. Oversample heavily so
    //  aliasing doesn't contaminate the harmonic readout here.
    // ----------------------------------------------------------------------
    printf ("[1] BIAS -> EVEN HARMONICS   (TEKK tanh, drive=3.0, fs=384k)\n");
    printf ("    expect: bias=0 -> 2nd/4th near -inf (odd only);\n");
    printf ("            bias>0 -> 2nd harmonic rises with bias\n\n");
    {
        const double fs = 384000.0;
        std::vector<double> in; int fb = binCentredTone (in, fs, 1000.0, 0.7, N);
        printf ("    %-8s %10s %10s %10s %10s\n", "bias", "H2(dB)", "H3(dB)", "H4(dB)", "H5(dB)");
        for (double bias : { 0.0, 0.2, 0.4, 0.7 })
        {
            TekkTanhStage s; s.setDrive (3.0f); s.setBias ((float) bias); s.prepare (fs);
            auto out = runStage ([&](float x){ return s.processSample (x); }, in);
            auto mag = magSpectrum (out);
            double f1 = mag[fb];
            printf ("    %-8.2f %10.1f %10.1f %10.1f %10.1f\n", bias,
                    db (mag[fb*2]/f1), db (mag[fb*3]/f1),
                    db (mag[fb*4]/f1), db (mag[fb*5]/f1));
        }
    }

    // ----------------------------------------------------------------------
    //  EXPERIMENT 2 -- ADAA suppresses aliasing at equal sample rate
    //  Same nonlinearity, same base rate (NO oversampling), naive point-sample
    //  vs 1st-order ADAA. High tone so harmonics fold.
    // ----------------------------------------------------------------------
    printf ("\n[2] ADAA -> LESS ALIASING   (drive=4.0, fs=48k, tone~6kHz, NO oversampling)\n");
    printf ("    expect: ADAA alias floor well below naive\n\n");
    {
        const double fs = 48000.0;
        std::vector<double> in; int fb = binCentredTone (in, fs, 6800.0, 0.9, N, true);
        const int nHarm = 8;

        // naive: raw biased tanh, point sampled (bypass ADAA via 0-length memory trick)
        auto naive = runStage ([drive=4.0](float xf){
            return (float)(std::tanh (drive * (double)xf) ); }, in);
        // ADAA version
        TekkTanhStage s; s.setDrive (4.0f); s.setBias (0.0f); s.prepare (fs);
        auto adaa = runStage ([&](float x){ return s.processSample (x); }, in);

        auto mN = magSpectrum (naive);
        auto mA = magSpectrum (adaa);
        printf ("    %-18s %12s\n", "method", "aliasFloor(dB)");
        printf ("    %-18s %12.1f\n", "naive (point)",  db (aliasFloor (mN, fb, nHarm)));
        printf ("    %-18s %12.1f\n", "ADAA 1st-order", db (aliasFloor (mA, fb, nHarm)));
        printf ("    improvement: %.1f dB\n",
                db (aliasFloor (mN, fb, nHarm)) - db (aliasFloor (mA, fb, nHarm)));
    }

    // ----------------------------------------------------------------------
    //  EXPERIMENT 3 -- transformer distorts LOWS more than HIGHS (1/omega flux)
    //  and that distortion BLOOMS with level. Voiced input-iron params; measured
    //  at a nominal (-20 dBFS) and a hot (-6 dBFS) tone. Oversampled readout, but
    //  the numbers are rate-independent by construction -- see experiment 5.
    // ----------------------------------------------------------------------
    printf ("\n[3] TRANSFORMER 1/omega + LEVEL BLOOM   (voiced input iron, fs=96k)\n");
    printf ("    expect: THD falls as frequency rises, and rises with level\n");
    printf ("            (clean midband/nominal; LF iron when pushed)\n\n");
    {
        const double fs = 96000.0;
        JilesAtherton::Params iron { 1.0, 0.12, 1.1e-3, 0.032, 0.70 };
        const double fluxDrive = 0.0009, leakHz = 20.0;
        printf ("    fluxDrive=%.4f, leak=%.0fHz\n", fluxDrive, leakHz);
        printf ("    %-10s %14s %14s\n", "freq(Hz)", "THD@-20dB(%)", "THD@-6dB(%)");
        for (double hz : { 40.0, 80.0, 160.0, 320.0, 1000.0, 5000.0 })
        {
            auto run = [&] (double amp)
            {
                std::vector<double> in; int fb = binCentredTone (in, fs, hz, amp, N);
                TransformerStage tf; tf.setParams (iron); tf.setFluxDrive (fluxDrive);
                tf.setLeakHz (leakHz); tf.prepare (fs);
                auto out = runStage ([&](float x){ return tf.processSample (x); }, in);
                auto mag = magSpectrum (out);
                return 100.0 * thd (mag, fb, 12);
            };
            printf ("    %-10.0f %14.3f %14.3f\n", hz, run (0.1), run (0.5));
        }
    }

    // ----------------------------------------------------------------------
    //  EXPERIMENT 4 -- hysteresis is real memory (loop has nonzero area)
    //  Drive a slow sine, confirm M(H) traces a loop (output depends on dH/dt
    //  sign), not a single-valued curve.
    // ----------------------------------------------------------------------
    printf ("\n[4] HYSTERESIS LOOP AREA   (J-A core, 1 cycle)\n");
    printf ("    expect: nonzero loop area => genuine memory, not a static curve\n\n");
    {
        JilesAtherton ja; ja.setParams ({ 1.0, 0.12, 1.1e-3, 0.032, 0.70 }); ja.reset();
        const int pts = 4000; double area = 0.0, Hp = 0.0, Mp = 0.0;
        double Mmax = -1e9, Mmin = 1e9;
        for (int i = 0; i <= pts; ++i)
        {
            double H = 8.0 * std::sin (2.0 * M_PI * i / pts);
            double M = ja.process (H);
            area += 0.5 * (M + Mp) * (H - Hp);      // shoelace contribution
            Hp = H; Mp = M;
            if (i > pts/8) { Mmax = std::max (Mmax, M); Mmin = std::min (Mmin, M); }
        }
        printf ("    loop area      = %.4f  (0 would mean no hysteresis)\n", std::abs (area));
        printf ("    M swing        = %.4f .. %.4f\n", Mmin, Mmax);
    }

    // ----------------------------------------------------------------------
    //  EXPERIMENT 5 -- iron character is SAMPLE-RATE INDEPENDENT
    //  The transformer integrator is dt-normalised, so the flux driving the
    //  core (hence the THD) is the same whether the plugin runs at 1x or 8x
    //  oversampling. Before that fix the core was driven ~fs harder and the
    //  voicing changed with every oversampling setting.
    // ----------------------------------------------------------------------
    printf ("\n[5] SAMPLE-RATE INDEPENDENCE   (voiced input iron, 40Hz, -6 dBFS)\n");
    printf ("    expect: THD nearly identical across rates => OS factor doesn't\n");
    printf ("            change the iron (flat column)\n\n");
    {
        JilesAtherton::Params iron { 1.0, 0.12, 1.1e-3, 0.032, 0.70 };
        printf ("    %-12s %14s\n", "fs(Hz)", "THD@40Hz(%)");
        for (double fs : { 48000.0, 96000.0, 192000.0, 384000.0 })
        {
            std::vector<double> in; int fb = binCentredTone (in, fs, 40.0, 0.5, N);
            TransformerStage tf; tf.setParams (iron); tf.setFluxDrive (0.0009);
            tf.setLeakHz (20.0); tf.prepare (fs);
            auto out = runStage ([&](float x){ return tf.processSample (x); }, in);
            auto mag = magSpectrum (out);
            printf ("    %-12.0f %14.3f\n", fs, 100.0 * thd (mag, fb, 12));
        }
    }

    // ----------------------------------------------------------------------
    //  EXPERIMENT 6 -- the TUBE character is EVEN-harmonic dominant
    //  Same tone/drive through the BJT long-tailed pair (TekkTanhStage, a
    //  symmetric odd nonlinearity) and the Koren triode (TriodeStage, an
    //  asymmetric load-line). The triode's asymmetry puts energy in H2; the
    //  symmetric tanh has H2 at the floor with H3 dominant.
    // ----------------------------------------------------------------------
    printf ("\n[6] TUBE vs BJT HARMONIC CHARACTER   (fs=384k, 1kHz, drive=4, bias=0)\n");
    printf ("    expect: triode H2 > H3 (even = tube);  tanh H3 > H2 (odd = BJT)\n\n");
    {
        const double fs = 384000.0;
        std::vector<double> in; int fb = binCentredTone (in, fs, 1000.0, 0.5, N);
        auto harm = [&] (ActiveStage& st)
        {
            st.prepare (fs);
            auto out = runStage ([&](float x){ return st.processSample (x); }, in);
            auto m = magSpectrum (out); double f1 = m[fb];
            return std::pair<double,double> (db (m[fb*2]/f1), db (m[fb*3]/f1));
        };
        TekkTanhStage tanhS; tanhS.setDrive (4.0f); tanhS.setBias (0.0f);
        TriodeStage   tubeS; tubeS.setDrive (4.0f); tubeS.setBias (0.0f);
        auto bjt  = harm (tanhS);
        auto tube = harm (tubeS);
        printf ("    %-16s %10s %10s %10s\n", "stage", "H2(dB)", "H3(dB)", "H2-H3");
        printf ("    %-16s %10.1f %10.1f %10.1f\n", "BJT tanh",     bjt.first,  bjt.second,  bjt.first  - bjt.second);
        printf ("    %-16s %10.1f %10.1f %10.1f\n", "Koren triode", tube.first, tube.second, tube.first - tube.second);
    }

    // ----------------------------------------------------------------------
    //  EXPERIMENT 7 -- transformer THROUGHPUT is unity at small signal
    //  Every experiment above measures ratios (THD, harmonic deltas), which is
    //  exactly how a -61 dB absolute level bug slipped through: the stage's
    //  linear gain was chi*fluxDrive, not the unity the design intended -- two
    //  transformers in series put the whole plugin at -120 dB ("no output").
    //  Now the stage normalises by the measured small-signal susceptibility,
    //  so the level must sit at ~0 dB for ANY fluxDrive and sample rate.
    // ----------------------------------------------------------------------
    printf ("\n[7] TRANSFORMER UNITY THROUGHPUT   (1kHz at -20 dBFS, voiced input iron)\n");
    printf ("    expect: ~0 dB for every fluxDrive and fs (iron knob changes\n");
    printf ("            saturation, not level)\n\n");
    {
        JilesAtherton::Params iron { 1.0, 0.12, 1.1e-3, 0.032, 0.70 };
        printf ("    %-12s %12s %12s\n", "fluxDrive", "fs(Hz)", "gain(dB)");
        for (double fd : { 0.0003, 0.0009, 0.0035 })
            for (double fs : { 48000.0, 192000.0 })
            {
                std::vector<double> in; int fb = binCentredTone (in, fs, 1000.0, 0.1, N);
                (void) fb;
                TransformerStage tf; tf.setParams (iron); tf.setFluxDrive (fd);
                tf.setLeakHz (20.0); tf.prepare (fs);
                auto out = runStage ([&](float x){ return tf.processSample (x); }, in);
                double si = 0.0, so = 0.0;
                for (size_t n = N / 4; n < in.size(); ++n)      // skip leak settle
                { si += in[n] * in[n]; so += out[n] * out[n]; }
                printf ("    %-12.4f %12.0f %12.2f\n", fd, fs, db (std::sqrt (so / si)));
            }
    }

    printf ("\n================================================================\n");
    printf ("  done.\n");
    printf ("================================================================\n");
    return 0;
}
