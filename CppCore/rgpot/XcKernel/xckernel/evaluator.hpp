#pragma once
// Host override of the generated evaluator. Same API as
// third_party/libxckernel/include/xckernel/evaluator.hpp. Stage A/B
// accumulate in long double so double kernels match NumPy einsum at
// the paper C-vs-NumPy bar (1e-16). The generated loop is left-to-right
// double and lands a few ulp off that exclusive bar.

#include <cstdint>

#if defined(__CUDACC__) || defined(__HIPCC__)
#define XCK_HD __host__ __device__
#else
#define XCK_HD
#endif

namespace xckernel {

template <typename T, typename Txc = T>
XCK_HD inline void stage_a(int64_t npts, int64_t nm,
                           const double* cf, const int32_t* off,
                           const uint16_t* fid, int64_t nfld,
                           const T* const* fields, const Txc* const* xc,
                           T* c) {
    for (int64_t g = 0; g < npts; ++g) {
        T acc = T(0);
        for (int64_t m = 0; m < nm; ++m) {
            T t = T(cf[m]);
            for (int32_t f = off[m]; f < off[m + 1]; ++f) {
                const uint16_t id = fid[f];
                t *= (id < nfld) ? fields[id][g] : T(xc[id - nfld][g]);
            }
            acc += t;
        }
        c[g] = acc;
    }
}

template <>
XCK_HD inline void stage_a<double, double>(int64_t npts, int64_t nm,
                                           const double* cf, const int32_t* off,
                                           const uint16_t* fid, int64_t nfld,
                                           const double* const* fields,
                                           const double* const* xc,
                                           double* c) {
    for (int64_t g = 0; g < npts; ++g) {
        long double acc = 0.0L;
        for (int64_t m = 0; m < nm; ++m) {
            long double t = static_cast<long double>(cf[m]);
            for (int32_t f = off[m]; f < off[m + 1]; ++f) {
                const uint16_t id = fid[f];
                t *= (id < nfld) ? fields[id][g] : xc[id - nfld][g];
            }
            acc += t;
        }
        c[g] = static_cast<double>(acc);
    }
}

template <typename T>
XCK_HD inline void stage_b(int64_t npts, int64_t nbf, const T* U, const T* c,
                           const T* V, T* out) {
    for (int64_t u = 0; u < nbf; ++u) {
        for (int64_t v = 0; v < nbf; ++v) {
            T s = T(0);
            const T* Ug = U + u * npts;
            const T* Vg = V + v * npts;
            for (int64_t g = 0; g < npts; ++g) {
                s += Ug[g] * c[g] * Vg[g];
            }
            out[u * nbf + v] += s;
        }
    }
}

template <>
XCK_HD inline void stage_b<double>(int64_t npts, int64_t nbf, const double* U,
                                   const double* c, const double* V,
                                   double* out) {
    for (int64_t u = 0; u < nbf; ++u) {
        for (int64_t v = 0; v < nbf; ++v) {
            long double s = 0.0L;
            const double* Ug = U + u * npts;
            const double* Vg = V + v * npts;
            for (int64_t g = 0; g < npts; ++g) {
                s += static_cast<long double>(Ug[g]) * c[g] * Vg[g];
            }
            out[u * nbf + v] += static_cast<double>(s);
        }
    }
}

} // namespace xckernel
