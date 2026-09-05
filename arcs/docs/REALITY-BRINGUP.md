# Reality bring-up profile

For the first graphical QEMU milestone, ARCS uses the IP19 `realitylogo` span data from the pinned `gazecube/sgibootscreen-src` submodule.

Temporary bring-up geometry:

- `clogow = 512`
- `clogoh = 256`
- `clogodata = realitylogo`
- reference screen baseline: 1280x1024

The SGI standalone renderer remains unchanged above this assignment. The profile exists only to populate the historical `htp_state` fields expected by `drawBackground()`.

The submodule remains pinned to commit `35c0a973a4fda3fbedccb020f63d365f46ec12bf` for reproducible bring-up assets.

This Reality profile is intentionally chosen as the temporary default because it better matches the period targeted by the ARCS reconstruction than the later IP35 InfiniteReality styling.
