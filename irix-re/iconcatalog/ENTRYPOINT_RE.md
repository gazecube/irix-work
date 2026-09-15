# IconCatalog entrypoint recovery

Target: IRIX 6.5 `/usr/sbin/iconcatalog`.

`main` is exported at `0x10013088`, size `0x144`. The binary-backed startup sequence is:

```text
setlocale(LC_ALL, NULL/default)
XtSetLanguageProc(NULL, NULL, NULL)
prepare sigaction state
sigemptyset(...)
sigaction(SIGTSTP, ...)
construct two path/string fragments in a 1 KiB stack buffer
operator new(0xb0)
CatalogApp::CatalogApp("IconCatalog", &argc, argv, NULL, 0)
atexit(<binary cleanup callback>)
virtual call through the application run slot
return
```

The exact signal handler symbol and both path fragments still need GOT/string resolution; they are intentionally not guessed here. The constructor is `__ct__10CatalogAppGPcPiPPcP16XrmOptionDescReci` at `0x1000c02c`. The original N32 allocation is 0xb0 bytes; that is evidence about the IRIX object, not a host layout requirement.

The exported surface is 166 text functions. Primary classes recovered by symbol name include `CatalogApp`, `CatalogWindow`, `CatalogMenuCmd`, `CatalogPageData`, `CatalogPageDialog`, `CatalogMenuHandler`, `CatalogRefTarget`, and `CatalogOldRefTarget`.

Direct imports prove the application uses the existing ViewKit/Imd stack plus the icon-panel DnD target family. The executable target should therefore be wired only after those constructor interfaces are typed, rather than hiding missing dependencies behind a replacement catalog UI.
