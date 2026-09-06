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

While restarting binary-first archaeology for the IRIX `fm` reimplementation, the project context briefly collapsed three completely separate lines of work into one glorious mental blob:

```text
IID fm reconstruction
+ SGI ARCS/UEFI bringup
+ EFX II
```

For one moment, the file-manager reconstruction was being reasoned about as though firmware bringup and EFX II belonged in the same implementation thread as locating the original `/usr/sbin/fm` specimen.

The correction was immediate:

> Woah, woah, woah, this isn't the ARCs thing, you good?

No ARCS firmware was implemented in `fm`, and EFX II was not smuggled into the file manager either.

Lesson: when several SGI-flavored projects are active at once, confirm which completely unrelated subsystem we are currently reverse engineering before trying to merge the universe.

## AI Somehow Continued After The Chat Locked

During the IID desktop reconstruction, the chat locked while the active work item was bringing the recovered `ImdModelBasedIconPanel` model-enumeration path online.

After starting a new chat and restoring context from an export, the repository was checked again and had somehow advanced beyond the handoff point. The `irix-re` branch now contained a fresh commit:

```text
50145c359d525bd1247d1bcde3e9f9ee79787cd3
libimdIconPanel: link recovered model API
```

The recovered `showChildren()` implementation had already been changed from merely storing a model pointer to actually iterating children through the binary-recovered API:

```text
createChildIterator()
getNextChild()
destroyChildIterator()
```

In other words, the AI appeared to have continued the reverse-engineering task after the conversation itself had become unavailable.

User reaction:

> looks like the previous AI already started after the chat locked, which is *very* trippy.

Whether this was delayed tool execution, an already-started action completing, or some other session-continuity oddity, the observable result was hilarious: the code had moved forward while the conversation looked dead.

Lesson: always check the branch tip after a chat lock. Apparently the repo may know things the chat does not.

## Museum Policy

If a future AI-assisted archaeology session produces a sufficiently funny, harmless, and memorable mixup, add it here.

Do not use this file for technical conclusions. Binary-confirmed findings belong in the reconstruction notes and source annotations.
