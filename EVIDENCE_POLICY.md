# Clogin cleanroom evidence policy

This project reconstructs SGI IRIX Clogin from shipped runtime artifacts while keeping leaked source code out of the implementation corpus.

## Allowed evidence

The following may be retained and studied as reverse-engineering reference material:

- original shipped executables and shared libraries;
- app-defaults and other X resources;
- icons, images, FTR databases, and configuration files;
- manpages and administrator-facing runtime documentation;
- symbol tables, dynamic symbol names, addresses, relocations, and ABI metadata;
- disassembly and independently written notes describing observable binary behavior;
- black-box behavioral traces and screenshots;
- independently authored reconstruction code.

Original binary evidence should live in a clearly marked evidence/reference area when it is stored with the project. It must not be represented as reconstructed or BSD-licensed source.

## Disallowed implementation input

Leaked IRIX source code is not an implementation reference for this cleanroom reconstruction.

Do not copy leaked-source text into:

- C or C++ implementation files;
- headers;
- comments;
- documentation;
- tests or fixtures intended to reproduce implementation logic.

Do not mechanically translate leaked function bodies into reconstructed code.

## Contamination auditing

A leaked source tree may be compared mechanically against the reconstruction only to detect accidental textual contamination. Such auditing is one-way: the leaked tree is a rejection oracle, not an implementation source.

The audit may report hashes, matching windows, paths, and contamination findings. It should not import leaked-source passages into the reconstruction repository.

## Preferred reconstruction order

1. Start from the original shipped binary or runtime artifact.
2. Recover exported names, ABI facts, strings, resources, addresses, and control flow.
3. Record behavioral conclusions in original wording only where that wording comes from shipped runtime resources; otherwise describe conclusions independently.
4. Implement the minimum behavior supported by that evidence.
5. Mark unresolved semantics as unresolved rather than filling them in by intuition.
6. Use leaked source only to check that the resulting repository does not contain copied text.

## Current primary Clogin evidence

The currently examined shipped executable is:

```text
/usr/Cadmin/bin/clogin
SHA256 f5019266eb0ec29b65f3bfec4d0b17a07cec9ef572aa3156888da65245b083cd
```

It is an IRIX 32-bit big-endian MIPS N32 dynamically linked ELF. Companion runtime evidence from the same sysadm payload includes:

```text
/usr/Cadmin/images/cloginlogo.rgb
/usr/lib/X11/app-defaults/Clogin
/var/sysadm/config/clogin.conf
/usr/sysadm/privbin/updateclogin
/usr/share/catman/u_man/cat1/clogin.z
```

These artifacts are evidence, not reconstructed source and not part of the source-code license.
