The EAM aluminium kernel evaluates four exponentials per neighbour entry
instead of up to eighteen: the density pass keeps rho(r) and rho'(r) for the
force pass, the shifts at the cutoff are computed once per call, and integer
powers and the embedding polynomial run without `pow`.
`scripts/validation/eam_al.py` (sympy) checks the rearranged forms and the
gather force against the closed form.
