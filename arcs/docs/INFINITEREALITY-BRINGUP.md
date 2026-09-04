# InfiniteReality bring-up profile

For the first graphical QEMU milestone, ARCS uses the `infiniterealitylogo` span data from the pinned `gazecube/sgibootscreen-src` submodule.

The temporary adapter only supplies the historical `htp_state` logo fields expected by the SGI standalone GUI:

- `clogow = 872`
- `clogoh = 174`
- `clogodata = infiniterealitylogo`

No SGI GUI rendering code is rewritten for this. `drawBackground()` continues to consume `htp_state` through the original interface.

The submodule is pinned to commit `35c0a973a4fda3fbedccb020f63d365f46ec12bf` so the bring-up image is reproducible.

This is intentionally only the logo selection for bring-up. Later IP35-specific company-string/color behavior should be ported from the appropriate source/profile data rather than guessed here.
