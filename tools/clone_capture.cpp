// ============================================================================
//  clone_capture : the TEKK Preamp CLONE capture pipeline (JUCE-free).
//
//  Workflow for cloning a real unit:
//    1) clone_capture signal sweep.wav        # make the test signal
//    2) play sweep.wav through the target unit, record its output -> rec.wav
//    3) clone_capture extract sweep.wav rec.wav clone.tekkcurve
//    4) load clone.tekkcurve in the plugin (or bake it into a header)
//
//  The extractor time-aligns the recording (latency search), bins output by
//  input level across the whole sweep, and averages -- which also collapses the
//  unit's thin reactive hysteresis into the mean static curve (the transformer
//  memory is modelled separately by the J-A cores around the clone stage).
//
//  Build:  g++ -O2 -std=c++17 -I dsp -I harness -I tools tools/clone_capture.cpp -o clone_capture
//
//  Sub-commands:
//    signal  <out.wav> [secs] [freq] [fs] [amp]
//    extract <ref.wav> <rec.wav> <out.tekkcurve> [N] [range]
//    bake    <out.h> [N] [range]          # exact reference-unit curve -> header
//    selftest                             # simulated round-trip + assertions
// ============================================================================
#include "CloneCurve.h"
#include "ActiveStage.h"
#include "WavIO.h"
#include "SimpleFFT.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <cmath>
#include <random>
#include <string>
#include <functional>
#include <algorithm>

// ---- the "reference unit" --------------------------------------------------
//  A documented static nonlinearity standing in for hardware: asymmetric (a
//  bias term -> even harmonics) plus a soft compressive knee (-> odd), scaled
//  to roughly unity small-signal gain. The selftest proves the capture pipeline
//  reconstructs THIS curve from a noisy, delayed recording; `bake` emits it as
//  the plugin's default clone.
static double device (double x)
{
    const double bias = 0.18;
    const double even = std::tanh (1.3 * x + bias) - std::tanh (bias);   // -> H2
    const double comp = x / std::sqrt (1.0 + 0.35 * x * x);              // soft -> odd
    return 0.78 * (0.6 * even + 0.55 * comp);
}

static std::vector<float> genReference (int fs, double seconds, double freq, double amp)
{
    const long N = (long) (seconds * fs);
    std::vector<float> x ((size_t) N);
    for (long n = 0; n < N; ++n)
        x[(size_t) n] = (float) (amp * std::sin (2.0 * M_PI * freq * (double) n / fs));
    return x;
}

// coarse integer latency: lag d (samples) that best aligns rec[n] ~ device(ref[n-d])
static int estimateLatency (const std::vector<float>& ref, const std::vector<float>& rec, int maxLag)
{
    const int M = (int) std::min (ref.size(), rec.size());
    double best = -1e300; int bestD = 0;
    for (int d = 0; d <= maxLag && d < M; ++d)
    {
        double s = 0.0;
        for (int n = d; n < M; n += 9) s += (double) ref[(size_t)(n - d)] * (double) rec[(size_t) n];
        if (s > best) { best = s; bestD = d; }
    }
    return bestD;
}

static void fillGaps (std::vector<float>& y, const std::vector<char>& have)
{
    const int N = (int) y.size();
    int first = 0; while (first < N && ! have[(size_t) first]) ++first;
    if (first == N) return;                               // nothing known
    int last = N - 1; while (last >= 0 && ! have[(size_t) last]) --last;
    for (int k = 0; k < first; ++k)    y[(size_t) k] = y[(size_t) first];   // flat extrapolate
    for (int k = last + 1; k < N; ++k) y[(size_t) k] = y[(size_t) last];
    int i = first;
    while (i <= last)
    {
        if (have[(size_t) i]) { ++i; continue; }
        int a = i - 1, b = i; while (b <= last && ! have[(size_t) b]) ++b;
        for (int k = a + 1; k < b; ++k)
        {
            double t = (double) (k - a) / (b - a);
            y[(size_t) k] = (float) (y[(size_t) a] * (1.0 - t) + y[(size_t) b] * t);
        }
        i = b;
    }
}

static CloneCurve extractCurve (const std::vector<float>& ref, const std::vector<float>& rec,
                                int N, double range, int delay)
{
    std::vector<double> sum ((size_t) N, 0.0), cnt ((size_t) N, 0.0);
    const int M = (int) std::min (ref.size(), rec.size());
    for (int n = delay; n < M; ++n)
    {
        const double x = ref[(size_t) (n - delay)], yv = rec[(size_t) n];
        if (x < -range || x > range) continue;
        int idx = (int) std::lround ((x + range) / (2.0 * range) * (N - 1));
        idx = std::max (0, std::min (N - 1, idx));
        sum[(size_t) idx] += yv; cnt[(size_t) idx] += 1.0;
    }
    CloneCurve c; c.range = (float) range; c.samples.assign ((size_t) N, 0.0f);
    std::vector<char> have ((size_t) N, 0);
    for (int i = 0; i < N; ++i)
        if (cnt[(size_t) i] > 0.0)
        { c.samples[(size_t) i] = (float) (sum[(size_t) i] / cnt[(size_t) i]); have[(size_t) i] = 1; }
    fillGaps (c.samples, have);
    return c;
}

// light 3-tap polish (kills residual capture noise; preserves shape)
static void smooth (std::vector<float>& y)
{
    if (y.size() < 3) return;
    std::vector<float> z = y;
    for (size_t i = 1; i + 1 < y.size(); ++i)
        z[i] = 0.25f * y[i-1] + 0.5f * y[i] + 0.25f * y[i+1];
    y.swap (z);
}

static double toneTHD (const std::function<float(float)>& fn, double fs, double hz, double amp)
{
    const int Nf = 1 << 15;
    int bin = (int) std::round (hz * Nf / fs); double f = bin * fs / (double) Nf;
    std::vector<double> out ((size_t) Nf);
    for (int n = 0; n < Nf; ++n) out[(size_t) n] = fn ((float) (amp * std::sin (2.0 * M_PI * f * n / fs)));
    auto mag = magSpectrum (out);
    double fund = mag[(size_t) bin], s = 0.0;
    for (int h = 2; h <= 10; ++h) { int b = bin * h; if (b < (int) mag.size()) s += mag[(size_t) b] * mag[(size_t) b]; }
    return 100.0 * std::sqrt (s) / std::max (fund, 1e-300);
}

static bool writeText (const std::string& path, const std::string& s)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return false;
    std::fwrite (s.data(), 1, s.size(), f); std::fclose (f);
    return true;
}

// ---- exact reference curve (no capture) used for the baked default ---------
static CloneCurve referenceCurve (int N, double range)
{
    CloneCurve c; c.range = (float) range; c.samples.resize ((size_t) N);
    for (int i = 0; i < N; ++i)
    {
        double x = -range + 2.0 * range * i / (N - 1);
        c.samples[(size_t) i] = (float) device (x);
    }
    return c;
}

// ===========================================================================
static int cmdSignal (int argc, char** argv)
{
    const std::string out = argv[2];
    const double secs = argc > 3 ? std::atof (argv[3]) : 6.0;
    const double freq = argc > 4 ? std::atof (argv[4]) : 55.0;
    const int    fs   = argc > 5 ? std::atoi (argv[5]) : 48000;
    const double amp  = argc > 6 ? std::atof (argv[6]) : 0.98;
    wav::Audio a; a.fs = fs; a.mono = genReference (fs, secs, freq, amp);
    if (! wav::write (out, a)) { std::fprintf (stderr, "cannot write %s\n", out.c_str()); return 1; }
    std::printf ("wrote %s  (%.1fs @ %dHz tone, fs=%d, amp=%.2f)\n", out.c_str(), secs, (int) freq, fs, amp);
    std::printf ("  -> play this through the target unit, record its output, then:\n");
    std::printf ("     clone_capture extract %s rec.wav clone.tekkcurve\n", out.c_str());
    return 0;
}

static int cmdExtract (int argc, char** argv)
{
    if (argc < 5) { std::fprintf (stderr, "usage: extract <ref.wav> <rec.wav> <out.tekkcurve> [N] [range]\n"); return 2; }
    const int    N     = argc > 5 ? std::atoi (argv[5]) : 2048;
    const double range = argc > 6 ? std::atof (argv[6]) : 4.0;
    wav::Audio ref, rec;
    if (! wav::read (argv[2], ref)) { std::fprintf (stderr, "cannot read %s\n", argv[2]); return 1; }
    if (! wav::read (argv[3], rec)) { std::fprintf (stderr, "cannot read %s\n", argv[3]); return 1; }
    int d = estimateLatency (ref.mono, rec.mono, 4096);
    CloneCurve c = extractCurve (ref.mono, rec.mono, N, range, d);
    smooth (c.samples);
    if (! writeText (argv[4], c.serialize())) { std::fprintf (stderr, "cannot write %s\n", argv[4]); return 1; }
    std::printf ("extracted %d-pt curve (range +-%.1f, latency %d smp) -> %s\n", N, range, d, argv[4]);
    return 0;
}

static int cmdBake (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: bake <out.h> [N] [range]\n"); return 2; }
    const int    N     = argc > 3 ? std::atoi (argv[3]) : 2048;
    const double range = argc > 4 ? std::atof (argv[4]) : 4.0;
    CloneCurve c = referenceCurve (N, range);
    if (! writeText (argv[2], c.toCppHeader ("DefaultCloneCurve", "kCurve")))
    { std::fprintf (stderr, "cannot write %s\n", argv[2]); return 1; }
    std::printf ("baked %d-pt reference curve (range +-%.1f) -> %s\n", N, range, argv[2]);
    return 0;
}

static int cmdSelftest()
{
    std::printf ("================================================================\n");
    std::printf ("  CLONE CAPTURE PIPELINE -- simulated round-trip\n");
    std::printf ("================================================================\n\n");

    const int    fs    = 96000, N = 2048;
    const double secs  = 5.0, freq = 55.0, range = 4.0;
    const int    delay = 137;                  // injected recording latency (samples)
    const double noise = 0.0010;               // recording noise (~ -60 dBFS)

    auto ref = genReference (fs, secs, freq, range);   // amplitude = range -> full coverage

    std::mt19937 rng (12345);
    std::normal_distribution<double> gauss (0.0, noise);
    std::vector<float> rec ((size_t) ((long) ref.size() + delay), 0.0f);
    for (size_t n = 0; n < ref.size(); ++n)
        rec[n + delay] = (float) (device (ref[n]) + gauss (rng));

    const int dHat = estimateLatency (ref, rec, 1024);
    CloneCurve c = extractCurve (ref, rec, N, range, dHat);
    smooth (c.samples);

    // (1) curve fidelity vs the true device
    double maxe = 0.0, rmse = 0.0;
    for (int i = 0; i < N; ++i)
    {
        double x = -range + 2.0 * range * i / (N - 1);
        double e = c.samples[(size_t) i] - device (x);
        maxe = std::max (maxe, std::fabs (e)); rmse += e * e;
    }
    rmse = std::sqrt (rmse / N);

    std::printf ("[1] CURVE RECONSTRUCTION (vs true device, noise=%.4f, delay=%d)\n", noise, delay);
    std::printf ("    latency recovered : %d  (injected %d)\n", dHat, delay);
    std::printf ("    max abs error     : %.5f\n", maxe);
    std::printf ("    rms error         : %.6f\n\n", rmse);

    // (2) harmonic match: cloned LUT vs device, at high fs (ADAA ~ point sample)
    LutCloneStage lut; lut.loadCurve (c.samples, c.range); lut.prepare (384000.0);
    std::printf ("[2] HARMONIC MATCH @1kHz, fs=384k (clone LUT vs device)\n");
    std::printf ("    %-8s %12s %12s\n", "amp", "device THD%", "clone THD%");
    double maxThdErr = 0.0;
    for (double A : { 0.3, 1.0, 2.5 })
    {
        double dt = toneTHD ([](float x){ return (float) device (x); }, 384000.0, 1000.0, A);
        lut.reset();
        double ct = toneTHD ([&](float x){ return lut.processSample (x); }, 384000.0, 1000.0, A);
        std::printf ("    %-8.2f %12.4f %12.4f\n", A, dt, ct);
        maxThdErr = std::max (maxThdErr, std::fabs (dt - ct));
    }
    std::printf ("    max THD delta     : %.4f %%\n\n", maxThdErr);

    // (3) round-trip the file format
    CloneCurve back = CloneCurve::parse (c.serialize());
    bool fileOk = back.valid() && back.samples.size() == c.samples.size()
                  && std::fabs (back.range - c.range) < 1e-6f;
    std::printf ("[3] FILE FORMAT round-trip (serialize -> parse) : %s\n\n", fileOk ? "ok" : "FAIL");

    const bool pass = (dHat == delay) && (maxe < 5e-3) && (rmse < 1e-3)
                      && (maxThdErr < 0.5) && fileOk;
    std::printf ("================================================================\n");
    std::printf ("  %s\n", pass ? "PASS -- capture pipeline reconstructs the unit." : "FAIL");
    std::printf ("================================================================\n");
    return pass ? 0 : 1;
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr,
        "usage: clone_capture <signal|extract|bake|selftest> ...\n"); return 2; }
    const std::string cmd = argv[1];
    if (cmd == "signal"   && argc >= 3) return cmdSignal (argc, argv);
    if (cmd == "extract")               return cmdExtract (argc, argv);
    if (cmd == "bake")                  return cmdBake (argc, argv);
    if (cmd == "selftest")              return cmdSelftest();
    std::fprintf (stderr, "unknown or malformed command: %s\n", cmd.c_str());
    return 2;
}
