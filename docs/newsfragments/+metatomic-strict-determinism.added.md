MetatomicConfig gains an explicit ``torch_determinism`` policy
(``TorchDeterminismPolicy::Fast`` default, ``Strict`` opt-in). Strict mode
enables deterministic LibTorch algorithms, math-only scaled-dot-product
attention (flash / memory-efficient / cuDNN SDP disabled), deterministic
cuDNN with benchmarking off, deterministic fill of uninitialized memory, and
disables TF32 for cuBLAS and cuDNN. These flags are process-global via
``at::globalContext()``; Fast never mutates them. CUDA hosts still need
``CUBLAS_WORKSPACE_CONFIG=:4096:8`` (or ``:16:8``) before the first cuBLAS
call for bit-stable matmuls.
