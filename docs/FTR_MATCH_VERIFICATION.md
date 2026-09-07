# FTR MATCH verification status

This note records the evidence boundary for the reconstructed FTR MATCH evaluator.

## Policy

Original IRIX binaries, DWARF, shipped resources, documentation, and observed runtime behavior are primary evidence. Reconstructed behavior is not promoted to verified status merely because a local regression test agrees with the reconstruction.

## Current status

The semantic changes introduced by commit `b5577dc718de13a62c01d632444c660a166aa822` were reverted pending binary verification. The expanded smoke assertions added immediately afterward were also removed because they encoded the same unverified assumptions and therefore were not an independent oracle.

The following behaviors remain UNVERIFIED until supported directly by original artifacts:

- whether `ascii` examines exactly the first 512 bytes
- whether `glob()` always matches only the basename
- whether brace expansion such as `*.{c,h}` is supported
- whether `dircontains()` applies only when the subject itself is a directory
- whether an untagged file yields `tag == -1`
- strict mixed-type comparison failure semantics
- invalid shift-count rejection semantics
- exact parser failure propagation for invalid intermediate values

## Verified archaeology infrastructure

The Apple/FTR archaeology workflow remains intact. It successfully extracts the original N32 FTR binaries, recovers symbols and DWARF, enumerates Apple-handler compile-unit functions, disassembles exact function ranges, inspects older implementations, and publishes the resulting report.

Use that evidence path to verify each semantic claim individually before reintroducing behavior into `FtrMatchEvaluator.c++`.
