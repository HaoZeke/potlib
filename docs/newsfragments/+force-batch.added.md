`Potential::forceBatch` evaluates several independent systems in one call.
The default `forceBatchImpl` loops over `forceImpl`, so every existing
kernel answers a batch without an override. When the result cache is on,
hits are served per system and only the misses reach the kernel as one
compact batch.
