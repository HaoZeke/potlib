The Cap'n Proto schema ships inside `librgpot` instead of a separate
`libptlrpc.so`. Consumers that bundle the umbrella alone -- pip wheels
especially -- no longer need a second shared object beside it, and
`rgpot.pc` correspondingly stops listing `-lptlrpc`; the symbols are
unchanged and still exported from `librgpot`. `potserv` and
`pot_client_bridge` take the generated translation unit from the shared
archive rather than compiling their own copy.
