# FTR MATCH verification status

This verification work lives under `irix-re-verification/`. The preserved `irix-re/` tree is the exact incoming state from commit `8339d4ab934978df7aac8948d3409227561cd821` and should not be modified by this verification effort.

Original IRIX binaries, DWARF, shipped resources, documentation, and observed runtime behavior are primary evidence. Reconstructed behavior is not promoted to verified status merely because a local regression test agrees with the reconstruction.

## Current disputed semantics

The following behaviors remain UNVERIFIED until supported directly by original artifacts:

- whether `ascii` examines exactly the first 512 bytes
- whether `glob()` always matches only the basename
- whether brace expansion such as `*.{c,h}` is supported
- whether `dircontains()` applies only when the subject itself is a directory
- whether an untagged file yields `tag == -1`
- strict mixed-type comparison failure semantics
- invalid shift-count rejection semantics
- exact parser failure propagation for invalid intermediate values

The verification workspace contains the cleaned evaluator state used for evidence-driven comparison. Reintroduce behavior only after the original binaries, DWARF, shipped resources, documentation, or observed IRIX runtime behavior prove it.