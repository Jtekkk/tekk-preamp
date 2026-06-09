#pragma once
#include <vector>
#include <complex>
#include <cmath>

using cpx = std::complex<double>;

// in-place iterative radix-2 FFT; n must be a power of two
inline void fft (std::vector<cpx>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)        // bit reversal
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / (double) len;
        const cpx wlen (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            cpx w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const cpx u = a[i + k];
                const cpx v = a[i + k + len / 2] * w;
                a[i + k]            = u + v;
                a[i + k + len / 2]  = u - v;
                w *= wlen;
            }
        }
    }
}

// magnitude spectrum (single-sided, linear), length n/2
inline std::vector<double> magSpectrum (const std::vector<double>& x)
{
    std::vector<cpx> a (x.size());
    for (size_t i = 0; i < x.size(); ++i) a[i] = cpx (x[i], 0.0);
    fft (a);
    std::vector<double> m (x.size() / 2);
    for (size_t i = 0; i < m.size(); ++i) m[i] = std::abs (a[i]) / (x.size() / 2.0);
    return m;
}
