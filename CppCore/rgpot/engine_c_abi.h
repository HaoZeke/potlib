/**
 * @file engine_c_abi.h
 * @brief Generic C ABI for rgpot engine plugins.
 *
 * One symbol set for every heavy backend: an engine shared library
 * (libuma_engine.so, libmetatomic_engine.so, ...) exports these
 * functions, backend selection is which library the host dlopens, and
 * configuration travels as a Cap'n Proto flat-array message on the
 * shared Potentials.capnp wire (the engine names its root params
 * struct, e.g. UmaParams). Adding a backend therefore needs no new
 * host code and no new ABI header.
 *
 * Units: positions Angstrom, energy eV, forces eV/Angstrom.
 *
 * The optional coordinate transform lets the host inject a
 * pre-evaluation map over (positions, box) — for example the
 * molecular-box convention that recenters a gas-phase molecule in a
 * large fixed cell so compiled static-shape graphs see a constant
 * edge count. The engine applies it to its own scratch copy; the
 * caller's buffers are never mutated.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#ifdef RGPOT_ENGINE_BUILD
#define RGPOT_ENGINE_API __declspec(dllexport)
#else
#define RGPOT_ENGINE_API __declspec(dllimport)
#endif
#else
#define RGPOT_ENGINE_API __attribute__((visibility("default")))
#endif

typedef struct RgpotEnginePot RgpotEnginePot;

/**
 * Optional host-side transform applied by the engine to its scratch
 * copy of (positions, box) before evaluation. positions holds
 * 3*nAtoms doubles, box 9 doubles (row-major). Return 0 on success;
 * nonzero aborts the force call with that code.
 */
typedef int (*rgpot_engine_coord_transform)(void *user, long nAtoms,
                                            double *positions, double *box);

#define RGPOT_ENGINE_ABI_VERSION 1

RGPOT_ENGINE_API int rgpot_engine_abi_version(void);

RGPOT_ENGINE_API int rgpot_engine_available(void);

/**
 * Create a potential from a Cap'n Proto flat-array message whose root
 * struct is the engine's params arm in Potentials.capnp (UmaParams for
 * the UMA engine). config points at config_len bytes (word-aligned
 * capnp framing as produced by capnp::messageToFlatArray). Returns
 * NULL on failure with a message in errbuf.
 */
RGPOT_ENGINE_API RgpotEnginePot *rgpot_engine_create(const void *config,
                                                     size_t config_len,
                                                     char *errbuf,
                                                     size_t errlen);

RGPOT_ENGINE_API void rgpot_engine_destroy(RgpotEnginePot *pot);

/**
 * Energy + forces. transform (with transform_user) may be NULL;
 * variance may be NULL. Returns 0 on success.
 */
RGPOT_ENGINE_API int rgpot_engine_force(
    RgpotEnginePot *pot, long nAtoms, const double *positions,
    const int *atomicNrs, double *forces, double *energy, double *variance,
    const double *box, rgpot_engine_coord_transform transform,
    void *transform_user);

#ifdef __cplusplus
}
#endif
