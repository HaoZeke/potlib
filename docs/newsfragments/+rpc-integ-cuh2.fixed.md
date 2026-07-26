The RPC integration test compares the CuH2 energy and forces at the same
1e-6 tolerance `CuH2PotTest` uses, instead of asserting exact float
equality against values from the kernel the Fortran 2018 rewrite replaced.
Its error path also kills the server before draining its output: reading a
live server's stderr blocks until that process exits, so an assertion
failure did not report itself, it sat until the six-hour CI timeout. The
handler now prints the exception type and traceback as well, since
`AssertionError` stringifies to nothing.
