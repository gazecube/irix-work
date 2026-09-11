# IID / File Manager continuity handoff - 2026-09-10

This is a project continuity summary, not a byte-for-byte transcript.

## Canonical repository state

Repository: `gazecube/irix-work`

Branch: `irix-re`

Current FM checkpoint after direct source wiring:

- `1779d4d9be87967086f079faf593dd28faaec060` - `fm: replace bootstrap path buttons with ImdIconFinder`
- `b14fb6084d5bb508a431c2b361d69af6c6a823dc` - `fm: link reconstructed ImdIconFinder`

The preferred workflow is again direct source edits and direct pushes to `irix-re`. Do not use one-shot GitHub Actions workflows for routine reconstruction changes unless there is a specific reason.

## FM policy

The File Manager reconstruction is explicitly NOT being treated as a clean-room implementation. It is expected to remain private. Original binaries/resources remain the primary fidelity reference, but leaked IRIX source may be used as implementation evidence for FM when useful.

Do not automatically apply that policy to other public-facing reconstruction components. The earlier clean-room constraints remain relevant outside this explicitly scoped private FM work.

## Current runnable FM state

Build from the repository root with:

```sh
cmake -S irix-re -B build
cmake --build build -j
./build/fm/fm ~
```

The CMake build stages the preserved SGI `FileManager` app-default database and extracts the seven original sidebar bitmap controls from `desktop_eoe(1).tar`. The normal launcher is `./build/fm/fm`; the actual ELF is `fm.bin`.

The original sidebar bitmap controls are now visible:

- pathfinder
- iconic layout
- list layout
- tree layout
- column layout
- gallery mode
- shelf

The reconstructed `SgThumbWheel` widget itself was already visually validated. A later FM composition bug placed it at the bottom of the window. The corrected composition is now canonical in `FmMain.c++`: `scaleZoom` is a child of the vertical `rc2` control stack and `controlBoard` is no longer stretched down to the status line. The wheel therefore sits directly under the seven sidebar buttons.

## Finder row correction

The temporary `Up` and `Refresh` buttons were bootstrap inventions. The preserved `FileManager` resources do not define them as File Manager controls.

The tree already contains a binary-backed reconstruction of `ImdIconFinder` in:

- `irix-re/libimdApp/Finders/IconFinder.h`
- `irix-re/libimdApp/Finders/IconFinder.c++`

That constructor recreates the original path using `sgFinderWidgetClass`, the real finder text child, `iconFinderFrame`, and an embedded `ImdModelBasedIconPanel`.

FM now uses `ImdIconFinder("iconFinder", finderForm, 0)` in place of the fake Up/Refresh/text-field row. The finder text child remains wired to Enter-to-navigate, and directory changes update the finder through `ImdIconFinder::setContent()`.

`fm/CMakeLists.txt` now links `imdApp` so the reconstructed finder is part of the runnable FM target. Top-level CMake already orders `libimdApp` before `fm`.

## Selection state

The verified selection implementation was promoted from `irix-re-verification` into the real tree earlier in this session. The runnable FM uses:

- single-click selection
- Shift-click extend/toggle
- selection bookkeeping
- batched selection changes
- Select All
- multiple-selection mode

Relevant earlier commits included `8510eae2`, `860547e7`, and `589b4c81`.

## App-default and sidebar work

Recent build/UI commits in this session included:

- `3b732f33` - remove stale Clogin/Pandora probe from the `irix-re` ViewKit build
- `5bf61ac3` - add FM runtime app-defaults launcher
- `4bac10d7` - stage SGI app-defaults and bitmap sidebar
- `7a723949` - repair temporary ThumbWheel patch format

The temporary patch/workflow mechanism should not be treated as the preferred development path. The actual ThumbWheel and ImdIconFinder changes are now incorporated directly into canonical source.

## Original FM architecture already recovered

The long-term FM reconstruction target is much broader than a basic file browser. It includes desktop/object-management shell behavior: filesystem browser, desktop objects, FTR type/launcher behavior, shelf, tree/gallery/pathfinder navigation, removable/media handling, network-aware objects, drag/drop integration, icon metadata, and cross-application hooks.

Recovered model/view path:

```text
directory path
 -> ImdFileModel::dnaSelectorFromPath()
 -> ImdDNA("ImdDirModel", selector)
 -> ImdBroker::getModel()
 -> ImdModelBasedIconPanel::changeModel()
 -> NotifyReceiver
 -> ImdModelBasedIconView
 -> ImdInvertedTeeRenderer
 -> ImdIconLayout
```

Recovered FTR path:

```text
ImdModelBasedIconView::getType()
 -> broker->getModel()
 -> dynamic_cast<ImdTypedModel *>
 -> getAttr(0xEFFF0080)
 -> ImdAttrTypeDNA
```

`ImdTypedModel::getAttr(0xEFFF0080)` ultimately reaches the original FTR database path using `imdFtrLoadDB("/usr/lib/filetype/desktop.otr")` and `imdFtrIconTypeFromFileName()`.

## Source-tree scan result

The public IRIX 6.5.7m source tree and the separately supplied 6.5.5 archive were searched aggressively for top-level FM implementation files/classes such as `FmApp`, `FmWindow`, `ImdIconPanel`, and `ImdRefModel`. No complete File Manager source tree was found.

The 6.5.7m tree does contain genuine SGI programs using the IMD APIs, including source-level evidence for `ImdHostFinder`, `ImdViewerFinder`, `ImdDNA`, `ImdString`, `setContent()`, `getContent()`, and related application integration. This remains useful implementation evidence.

## XFile stopgap

`gazecube/xfile` remains a temporary daily-use file manager while FM becomes usable. It was modified to remove Xft usage, use SGI Screen bitmap fonts, and avoid the MaXX/system Motif mixed-library crash. Do not spend significant effort recreating SGI-only FM features inside XFile unless explicitly requested; those belong in FM.

## Immediate next FM work

After verifying the newly integrated `ImdIconFinder` visually and at runtime, continue toward daily usability:

1. Finish the core file operation callbacks: Add New Directory, Make Copy, Remove with confirmation, and Get Info.
2. Replace remaining placeholder layout callbacks with the recovered SGI layout classes/behavior.
3. Continue Pathfinder, Shelf, Tree, Columns, and Gallery reconstruction using original binaries/resources first, with leaked source allowed for this private FM branch.
4. Continue original FTR `desktop.otr` matcher reconstruction rather than inventing extension/MIME mappings.

## Important visual rule

Do not modify the already validated ThumbWheel drawing/hit-testing path without new binary evidence. Current remaining visual fixes should generally be treated as FM composition/resource problems first.
