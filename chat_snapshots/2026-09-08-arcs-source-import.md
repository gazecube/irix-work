# Project chat snapshot - 2026-09-08 - ARCS source import

## Current ARCS state

Generic branch: `arcs-uefi`.

Before this snapshot, `arcs-uefi` advanced from `3b915b66ac39a691a2f52019c193ee4f0165f686` to `154e4258cbc06a3cd62d2c8522820fae36b7ffa7` (`Promote generic SGI command parser groundwork`). That promotion brought the SGI parser/editor/string-list groundwork and required compatibility headers/EDK II BaseLib dependency from the MacBook work onto the generic branch. MacBook-only workflow and Apple/Snow-Leopard `bootstrap_compat.c` changes were deliberately excluded.

The promoted parser groundwork still has fidelity debt. `parser.c`, `stringlist.c`, and provisional `edit_gets.c` must be checked/corrected against pristine IRIX 6.5.7m source before the Command Monitor is considered faithful. Compatibility headers remain port scaffolding, not original SGI headers.

## Full IRIX source reference established

Authoritative source catalog:

`https://github.com/calmsacibis995/irix-657m-src/tree/main`

The project had already been using IRIX 6.5.7m source, but the current `arcs/sgisrc` directory was only a hand-picked imported subset. This caused an unnecessary search when original `stand/arcs/lib/libsk/graphics/htport.c` was absent from the subset.

New rule: absence from the imported subset never implies original source is unavailable. Check the full IRIX 6.5.7m source tree first. For ARCS, original source is the default implementation; reconstruction is only for unavoidable architecture/firmware boundaries.

Decision made in this chat: vendor the complete pristine upstream `stand/arcs` tree into `irix-work`, rather than continuing piecemeal source imports. Importing the full tree does NOT mean compiling all of it. EDK II will continue selecting only translation units that have been brought up on x86-64 UEFI.

Target layout:

```text
arcs/
  sgisrc/
    stand/arcs/          <- pristine IRIX 6.5.7m source catalog
  uefi/                  <- x86-64 UEFI compatibility/platform layer
```

The pristine tree should remain unchanged wherever physically possible. Architecture/firmware substitutions belong in UEFI-side shims, compatibility headers, include-path/build definitions, or narrowly documented adaptations.

## Textport / Command Monitor discovery

Original `stand/arcs/lib/libsk/graphics/htport.c`, revision 1.105, was recovered and verified against the IRIX 6.5.7m upstream tree.

It contains the real SGI graphical host textport engine: `txMap`, `txInit`, `txConfig`, `txReset`, `txPaint`, scrolling, character/control/CSI processing, `htp_init`, `htp_write`, `txSetJumpbuf`, `txSetString`, and `gethtp`.

Critical original behavior in `htp_write`: when text is written while the graphical UI is active, it calls `cleanGfxGui()`, resets the textport, and paints the SGI graphical textport. Therefore the Command Monitor must NOT use UEFI ConOut.

Correct path:

```text
menu_manualmode()
  -> p_textmode()
  -> p_clear()
  -> original printf/putchar
  -> ARCS Write(StandardOut,...)
  -> original htp_write()
  -> original SGI txStuff/txCtrlstuff/txCsistuff/txPaint
  -> GOP framebuffer backend
  -> original command_parser(cmd_table, ">> ", ...)
```

Current UEFI GUI state is already owned by `static ARCS_GFXGUI_BRINGUP Bringup` in `arcs/uefi/drawbackground_bringup.c`, with GUI initialized on `Bringup.Htp`. Original `htport.c` instead declares its own static `hosttextport` and global `htp` pointer. Do not create two live textports. The port must preserve the original textport engine while binding its `htp` state to the already initialized GOP-backed `Bringup.Htp` through the narrowest possible platform adaptation.

## Full-tree import in progress

A one-shot GitHub Actions importer was added to `arcs-uefi` because Git object SHAs cannot be directly reused across repositories. Commit:

`95fd4ef668d4aaf8ec0e64c877afc6d937429c63` - `Add one-shot pristine ARCS source importer`

Workflow run at snapshot time:

`34275085937` - `Import pristine IRIX ARCS source`

The workflow checks out `arcs-uefi`, clones `calmsacibis995/irix-657m-src`, copies the complete upstream `stand/arcs` tree to `arcs/sgisrc/stand/arcs`, commits it as `Import pristine IRIX 6.5.7m ARCS source tree`, and pushes it back to `arcs-uefi`. At snapshot time the run had started and was in progress.

After successful import, remove or disable the one-shot importer so future pushes do not perform source import work.

## Immediate next work

1. Verify the full `stand/arcs` import commit and remove/disable the one-shot workflow.
2. Use pristine upstream files as authoritative source for parser/stringlist/edit_gets fidelity corrections.
3. Bring original `htport.c` into the EDK II selected source set without creating a second textport state.
4. Bind original output path to the existing GOP-backed `Bringup.Htp`.
5. Implement the narrow ARCS `Write(StandardOut,...)`, `Read(StandardIn,...)`, `getc`, `ioctl`, and `isgraphic` compatibility boundary required by original code.
6. Bring in original `panel.c`, `menu_manualmode`, command table, and commands as source permits.
7. Reach the first genuine SGI `>>` in QEMU, then validate the generic Command Monitor on the user's MAIN PC before carrying it to the MacBook.

## Known-good constraints

- Do not touch EFI NVRAM for SGI environment persistence. Current ARCS env is RAM-backed; persistent design is file-backed later.
- Do not globally intercept RuntimeServices.
- Do not replace the SGI parser or command table with a fake EFI shell.
- Do not route Command Monitor output through UEFI ConOut when original graphical textport exists.
- Do not broadly refactor graphics/autoboot/Execute or disturb the known-good startup UI.
- Preserve Snow Leopard chainloading on `arcs-macbook`.
- Built-in MacBook trackpad remains unimplemented; inspect firmware exposure before adding support.
- First generic real-hardware Command Monitor target is the user's main PC, not the MacBook.
