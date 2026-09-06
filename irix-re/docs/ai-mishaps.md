# AI Archaeology Mishaps

A small museum of funny project-side mixups that happened while reverse engineering and rebuilding the IRIX Interactive Desktop stack.

These are intentionally kept separate from the serious reconstruction notes.

## The Blinky Detour

We spent enough time circling the visible Clogin blinker that it became a project landmark.

Eventually the correct status became:

> We were WAY past the blinky thing.

The blinker remains a future binary-backed investigation target, not the current work item.

## The Mysterious +0x24

During `ImdIconFinder` reconstruction, `+0x24` spent some time being interpreted too confidently before more binary evidence arrived.

Later archaeology showed that `getPocket()` returns the object stored at `+0x24`, which is also where the constructor stores the `ImdModelBasedIconPanel` instance.

Lesson: offsets are evidence; names are hypotheses until confirmed.

## Checking Current DVD Pinning Workflow

While continuing `libSgm` / `libimdIconPanel` archaeology, the conversation briefly detoured into whether IRIX had a native DVD player.

A later status check somehow became:

> Checking Current DVD Pinning Workflow

There was no DVD pinning workflow.

The actual work was still IID R.E. on the `irix-re` branch.

A historically plausible SGI-style `dvdplayer` application is, however, now deliberately pinned as a future project idea.

## WHY WAS MY TEXT SO BIG

At one point, markdown formatting turned an ordinary reaction into giant headline text.

The resulting response was exactly the kind of accidental presentation bug that deserves preservation here.

## Implementing ARCS Bringup In The File Manager

While restarting binary-first archaeology for the IRIX `fm` reimplementation, the project context briefly crossed wires with the separate SGI ARCS/UEFI bringup effort.

For one glorious moment, the file-manager reconstruction appeared to be heading toward firmware bringup instead of locating the original `/usr/sbin/fm` specimen.

The correction was immediate:

> Woah, woah, woah, this isn't the ARCs thing, you good?

No ARCS firmware was actually implemented in `fm`.

Lesson: when two SGI archaeology projects are active at once, confirm which century of the boot process we are currently reverse engineering.

## Museum Policy

If a future AI-assisted archaeology session produces a sufficiently funny, harmless, and memorable mixup, add it here.

Do not use this file for technical conclusions. Binary-confirmed findings belong in the reconstruction notes and source annotations.
