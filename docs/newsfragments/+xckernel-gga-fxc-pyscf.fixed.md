GGA TDA/RPA ``applyFxc`` now uses the PySCF ``nr_rks_fxc_st`` contract (``transform_fxc`` singlet 4x4, einsum, ``scale_ao``, ``hermi_sum``) instead of the generated 7-term monomials.
