TDA/RPA fxc stage A stays double; LDA wv is `w*rho*(v2rho2_0+v2rho2_1)`.
Stage B tiles the grid at PySCF `BLKSIZE` (128). `--tda-rpa` pins host
J from the `get_j` call inside `gen_vind` / `gen_tdhf_operation`.
