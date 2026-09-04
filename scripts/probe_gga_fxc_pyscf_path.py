#!/usr/bin/env python3
"""Prototype: PySCF GGA fxc contract vs committed TDA/RPA pins.

rg.terra only. Does not write goldens. Does not loosen 1e-17.
Uses stored st operands + transform_fxc(spin=1) + einsum/scale_ao/hermi_sum.
"""

from __future__ import annotations

import os
import socket
import sys
from pathlib import Path

for _thr in (
    "OMP_NUM_THREADS",
    "MKL_NUM_THREADS",
    "OPENBLAS_NUM_THREADS",
    "NUMEXPR_NUM_THREADS",
):
    os.environ.setdefault(_thr, "1")

import numpy as np

TERRA_HOSTS = {"rgam5terra", "rg.terra"}
ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "CppCore" / "tests" / "data" / "xckernel" / "pyscf_h2o_sto3g"
BLK = 128
BAR = 1e-17


def _require_terra() -> None:
    host = socket.gethostname().split(".")[0]
    if host not in TERRA_HOSTS and os.environ.get("XCKERNEL_ALLOW_REGEN") != "1":
        sys.exit(f"probe_gga_fxc_pyscf_path.py runs on rg.terra only (got {host})")


def max_rel(got: np.ndarray, ref: np.ndarray) -> tuple[float, float]:
    diff = np.max(np.abs(got - ref))
    den = max(float(np.max(np.abs(ref))), 1.0)
    return float(diff / den), float(diff)


def transition_dm_cpp(Co: np.ndarray, Cv: np.ndarray, z: np.ndarray, occ: float) -> np.ndarray:
    nao, nocc = Co.shape
    nvir = Cv.shape[1]
    tmp = np.zeros((nvir, nao))
    for a in range(nvir):
        for q in range(nao):
            acc = 0.0
            for i in range(nocc):
                acc += (occ * Co[q, i]) * z[i, a]
            tmp[a, q] = acc
    dm = np.zeros((nao, nao))
    for p in range(nao):
        for q in range(nao):
            acc = 0.0
            for a in range(nvir):
                acc += tmp[a, q] * Cv[p, a]
            dm[p, q] = acc
    return dm


def project_ov(Co: np.ndarray, Cv: np.ndarray, Vao: np.ndarray) -> np.ndarray:
    nao, nocc = Co.shape
    nvir = Cv.shape[1]
    tmp = np.zeros((nvir, nao))
    for a in range(nvir):
        for q in range(nao):
            acc = 0.0
            for p in range(nao):
                acc += Cv[p, a] * Vao[p, q]
            tmp[a, q] = acc
    ov = np.zeros((nocc, nvir))
    for i in range(nocc):
        for a in range(nvir):
            acc = 0.0
            for q in range(nao):
                acc += tmp[a, q] * Co[q, i]
            ov[i, a] = acc
    return ov


def rho1_from_dm(chi: np.ndarray, dchi: np.ndarray, dm: np.ndarray) -> np.ndarray:
    """Match XcKernel.cc apply_fxc_d: c0 = dm.T @ chi, c1 = dm @ chi."""
    nbf, ng = chi.shape
    c0 = np.zeros((nbf, ng))
    c1 = np.zeros((nbf, ng))
    for u in range(nbf):
        for g in range(ng):
            acc0 = 0.0
            acc1 = 0.0
            for v in range(nbf):
                acc0 += dm[v, u] * chi[v, g]
                acc1 += dm[u, v] * chi[v, g]
            c0[u, g] = acc0
            c1[u, g] = acc1
    rho1 = np.zeros((4, ng))
    for g in range(ng):
        acc = 0.0
        gx = gy = gz = 0.0
        for u in range(nbf):
            acc += chi[u, g] * c0[u, g]
            gx += c0[u, g] * dchi[0, u, g] + c1[u, g] * dchi[0, u, g]
            gy += c0[u, g] * dchi[1, u, g] + c1[u, g] * dchi[1, u, g]
            gz += c0[u, g] * dchi[2, u, g] + c1[u, g] * dchi[2, u, g]
        rho1[0, g] = acc
        rho1[1, g] = gx
        rho1[2, g] = gy
        rho1[3, g] = gz
    return rho1


def pack_libxc(op: np.lib.npyio.NpzFile) -> tuple[list, list]:
    """eval_xc spin=1 arrays from stored columns + closed-shell fill."""
    ng = int(op["w"].shape[0])

    def col(name: str, n: int) -> np.ndarray:
        out = np.zeros((ng, n))
        for i in range(n):
            key = f"{name}_{i}"
            if key in op.files:
                out[:, i] = np.ascontiguousarray(op[key])
        return out

    vrho = np.zeros((ng, 2))  # unused for fxc transform
    # C ABI names only (xck_gga_st_o2_p_scal_names). Closed-shell fill.
    vsigma = col("vsigma", 3)
    vsigma[:, 2] = vsigma[:, 0]
    v2rho2 = col("v2rho2", 3)
    v2rho2[:, 2] = v2rho2[:, 0]
    v2rhosigma = col("v2rhosigma", 6)
    v2rhosigma[:, 5] = v2rhosigma[:, 0]
    v2sigma2 = col("v2sigma2", 6)
    v2sigma2[:, 5] = v2sigma2[:, 0]
    vxc = [vrho, vsigma]
    fxc = [v2rho2, v2rhosigma, v2sigma2]
    return vxc, fxc


def blocked_chi_aow(chi: np.ndarray, aow: np.ndarray) -> np.ndarray:
    nbf, ng = chi.shape
    v = np.zeros((nbf, nbf))
    for g0 in range(0, ng, BLK):
        g1 = min(g0 + BLK, ng)
        v += chi[:, g0:g1] @ aow[:, g0:g1].T
    return v


def gga_st_fxc4_cpp(vs, frr, frg, fgg, ga) -> np.ndarray:
    """Byte-level twin of XcKernel.cc gga_st_fxc4."""
    vp = np.zeros((4, 4, 2, 2))
    vp[0, 0, 0, 0] = frr[0]
    vp[0, 0, 0, 1] = frr[1]
    vp[0, 0, 1, 0] = frr[1]
    vp[0, 0, 1, 1] = frr[2]
    M = np.array(
        [
            [fgg[0], fgg[1], fgg[2]],
            [fgg[1], fgg[3], fgg[4]],
            [fgg[2], fgg[4], fgg[5]],
        ]
    )
    tmp = np.zeros((3, 2, 2))
    for i in range(3):
        tmp[i, 0, 0] = 2.0 * M[i, 0]
        tmp[i, 0, 1] = M[i, 1]
        tmp[i, 1, 0] = M[i, 1]
        tmp[i, 1, 1] = 2.0 * M[i, 2]
    qgg_spin = np.zeros((2, 2, 2, 2))
    for b in range(2):
        for d in range(2):
            qgg_spin[0, 0, b, d] = 2.0 * tmp[0, b, d]
            qgg_spin[0, 1, b, d] = tmp[1, b, d]
            qgg_spin[1, 0, b, d] = tmp[1, b, d]
            qgg_spin[1, 1, b, d] = 2.0 * tmp[2, b, d]
    sfg = np.array([[2.0 * vs[0], vs[1]], [vs[1], 2.0 * vs[2]]])
    for x in range(3):
        for y in range(3):
            gxgy = ga[x] * ga[y]
            for b in range(2):
                for d in range(2):
                    acc = 0.0
                    for a in range(2):
                        for c in range(2):
                            acc += qgg_spin[a, b, c, d] * gxgy
                    if x == y:
                        acc += sfg[b, d]
                    vp[1 + x, 1 + y, b, d] = acc
    st = np.zeros((2, 2, 2))
    for r in range(2):
        uu, ud, dd = frg[r * 3 + 0], frg[r * 3 + 1], frg[r * 3 + 2]
        st[r, 0, 0] = 2.0 * uu
        st[r, 0, 1] = ud
        st[r, 1, 0] = ud
        st[r, 1, 1] = 2.0 * dd
    for x in range(3):
        for r in range(2):
            for b in range(2):
                acc = 0.0
                for a in range(2):
                    acc += st[r, a, b] * ga[x]
                vp[0, 1 + x, r, b] = acc
                vp[1 + x, 0, b, r] = acc
    fxc_s = np.zeros((4, 4))
    for x in range(4):
        for y in range(4):
            fxc_s[x, y] = vp[x, y, 0, 0] + vp[x, y, 0, 1]
    return fxc_s


def apply_fxc_pyscf(op, rho1: np.ndarray) -> np.ndarray:
    from pyscf.dft.xc_deriv import transform_fxc

    chi = np.ascontiguousarray(op["chi"])
    dchi = np.ascontiguousarray(op["dchi"])
    w = np.ascontiguousarray(op["w"])
    nbf, ng = chi.shape
    gx = np.ascontiguousarray(op["grad_rho_a_x"])
    gy = np.ascontiguousarray(op["grad_rho_a_y"])
    gz = np.ascontiguousarray(op["grad_rho_a_z"])
    rho_a = np.zeros((4, ng))
    rho_a[1] = gx
    rho_a[2] = gy
    rho_a[3] = gz
    rho = np.stack([rho_a, rho_a], axis=0)
    vxc, fxc_raw = pack_libxc(op)
    fxc = transform_fxc(rho, vxc, fxc_raw, "GGA", spin=1)
    fxc_s = fxc[0, :, 0, :, :] + fxc[0, :, 1, :, :]
    wv = np.einsum("yg,xyg,g->xg", rho1, fxc_s, w)
    wv = np.array(wv, copy=True)
    wv[0] *= 0.5
    aow = chi * wv[0] + dchi[0] * wv[1] + dchi[1] * wv[2] + dchi[2] * wv[3]
    v = blocked_chi_aow(chi, aow)
    v = v + v.T
    return v


def tda_sigma(Co, Cv, e_ia, z, vj, vxc, half: bool) -> np.ndarray:
    v1 = vj + (0.5 * vxc if half else vxc)
    sig = project_ov(Co, Cv, v1)
    return sig + e_ia * z


def main() -> int:
    _require_terra()
    op = np.load(DATA / "gga_st_operands.npz")
    mo = np.load(DATA / "gga_mo.npz")
    zs = np.load(DATA / "tda_gga_z.npy")
    tda_ref = np.load(DATA / "tda_gga_sigma_ref.npy")
    tda_j = np.load(DATA / "tda_gga_j.npy")
    Co = np.ascontiguousarray(mo["Co"])
    Cv = np.ascontiguousarray(mo["Cv"])
    e_ia = np.ascontiguousarray(mo["e_ia"])
    chi = np.ascontiguousarray(op["chi"])
    dchi = np.ascontiguousarray(op["dchi"])
    nz, nocc, nvir = zs.shape
    got_half = np.zeros_like(tda_ref)
    got_full = np.zeros_like(tda_ref)
    print("shapes", dict(chi=chi.shape, dchi=dchi.shape, zs=zs.shape, j=tda_j.shape))
    print("op keys", sorted(op.files)[:30], "...")
    from pyscf.dft.xc_deriv import transform_fxc

    vxc_pk, fxc_raw = pack_libxc(op)
    ng = int(op["w"].shape[0])
    rho_a = np.zeros((4, ng))
    rho_a[1] = np.ascontiguousarray(op["grad_rho_a_x"])
    rho_a[2] = np.ascontiguousarray(op["grad_rho_a_y"])
    rho_a[3] = np.ascontiguousarray(op["grad_rho_a_z"])
    rho = np.stack([rho_a, rho_a], axis=0)
    fxc_t = transform_fxc(rho, vxc_pk, fxc_raw, "GGA", spin=1)
    fxc_s_ref = fxc_t[0, :, 0, :, :] + fxc_t[0, :, 1, :, :]
    miss = 0.0
    for g in (0, ng // 2, ng - 1):
        vs = [vxc_pk[1][g, 0], vxc_pk[1][g, 1], vxc_pk[1][g, 2]]
        frr = [fxc_raw[0][g, 0], fxc_raw[0][g, 1], fxc_raw[0][g, 2]]
        frg = [fxc_raw[1][g, i] for i in range(6)]
        fgg = [fxc_raw[2][g, i] for i in range(6)]
        ga = [rho_a[1, g], rho_a[2, g], rho_a[3, g]]
        got = gga_st_fxc4_cpp(vs, frr, frg, fgg, ga)
        miss = max(miss, float(np.max(np.abs(got - fxc_s_ref[:, :, g]))))
        print("  fxc4 g", g, "maxabs", float(np.max(np.abs(got - fxc_s_ref[:, :, g]))))
    print("fxc4 twin maxabs", miss)
    for iz in range(nz):
        z = zs[iz]
        vj = tda_j[iz]
        dm = transition_dm_cpp(Co, Cv, z, 2.0)
        rho1 = rho1_from_dm(chi, dchi, dm)
        vxc = apply_fxc_pyscf(op, rho1)
        got_half[iz] = tda_sigma(Co, Cv, e_ia, z, vj, vxc, True)
        got_full[iz] = tda_sigma(Co, Cv, e_ia, z, vj, vxc, False)
        print(
            f"  z{iz} rho1 max",
            float(np.max(np.abs(rho1))),
            "vxc max",
            float(np.max(np.abs(vxc))),
        )
    rh, ah = max_rel(got_half, tda_ref)
    rf, af = max_rel(got_full, tda_ref)
    print(f"TDA einsum+hermi + 0.5*V  rel={rh:.6e} abs={ah:.6e} bar={BAR}")
    print(f"TDA einsum+hermi + 1.0*V  rel={rf:.6e} abs={af:.6e} bar={BAR}")
    print("half pass" if rh <= BAR else "half FAIL")
    print("full pass" if rf <= BAR else "full FAIL")

    xys = np.load(DATA / "rpa_gga_xy.npy")
    rpa_ref = np.load(DATA / "rpa_gga_sigma_ref.npy")
    rpa_j = np.load(DATA / "rpa_gga_j.npy")
    got_rpa = np.zeros_like(rpa_ref)
    nao = Co.shape[0]
    nov = nocc * nvir
    for iz in range(nz):
        xy = xys[iz]
        x = xy[0]
        y = xy[1]
        dm = transition_dm_cpp(Co, Cv, x, 2.0)
        # rpaTransitionDm adds Y term: einsum po,ov,qv
        tmp = np.zeros((Cv.shape[1], nao))
        for a in range(Cv.shape[1]):
            for p in range(nao):
                acc = 0.0
                for i in range(nocc):
                    acc += (2.0 * Co[p, i]) * y[i, a]
                tmp[a, p] = acc
        for p in range(nao):
            for q in range(nao):
                acc = 0.0
                for a in range(Cv.shape[1]):
                    acc += tmp[a, p] * Cv[q, a]
                dm[p, q] += acc
        rho1 = rho1_from_dm(chi, dchi, dm)
        vxc = apply_fxc_pyscf(op, rho1)
        v1 = rpa_j[iz] + 0.5 * vxc
        top = project_ov(Co, Cv, v1) + e_ia * x
        bot = np.zeros_like(y)
        tmp = np.zeros((Cv.shape[1], nao))
        for a in range(Cv.shape[1]):
            for p in range(nao):
                acc = 0.0
                for q in range(nao):
                    acc += Cv[q, a] * v1[p, q]
                tmp[a, p] = acc
        for i in range(nocc):
            for a in range(Cv.shape[1]):
                acc = 0.0
                for p in range(nao):
                    acc += tmp[a, p] * Co[p, i]
                bot[i, a] = -(e_ia[i, a] * y[i, a] + acc)
        got_rpa[iz, 0] = top
        got_rpa[iz, 1] = bot
    rr, ar = max_rel(got_rpa, rpa_ref)
    print(f"RPA einsum+hermi + 0.5*V  rel={rr:.6e} abs={ar:.6e} bar={BAR}")
    print("rpa pass" if rr <= BAR else "rpa FAIL")
    return 0 if rh <= BAR and rr <= BAR else 1


if __name__ == "__main__":
    raise SystemExit(main())
