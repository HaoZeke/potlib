MetatomicConfig gains an explicit ``torch_determinism`` policy
(``TorchDeterminismPolicy::Fast`` default, ``Strict`` opt-in). Strict mode
enables deterministic LibTorch algorithms, math-only scaled-dot-product
attention (flash / memory-efficient / cuDNN SDP disabled), and disables TF32
for cuBLAS and cuDNN. These flags are process-global via
``at::globalContext()``; Fast never mutates them.
