# Desktop entrypoint recovery

Target: IRIX 6.5 `/usr/sbin/desktop`.

`main` is exported at `0x1000900c`, size `0xa8`. Its complete control flow is recovered:

```text
setlocale(LC_ALL, <binary empty/default locale string>)
operator new(0x94)
FmApp::FmApp("Desktop", &argc, argv, NULL, 0)
virtual call through the VkApp run slot
return
```

The constructor symbol is `__ct__5FmAppGPcPiPPcP16XrmOptionDescReci` at `0x10007bec`, size `0x274`. The 0x94 allocation is the original N32 object size and must not be reused as a host C++ object size.

The final virtual dispatch loads the object's primary base/vtable adjustment through the SGI C++ ABI and reaches the same application-run family already recovered from `libvk.so.1`. The existing ViewKit binary work proves `VkApp::run()` performs `run_first()` followed by the ordinary Xt event loop.

This makes `FmApp` the first real desktop source-unit target. The remaining exported application surface is small enough for a complete pass: 68 defined text symbols, centered on `FmApp`, `PanelComponent`, `basePanel`, `FmPanel`, `ResetDialog`, and `UmaskDlg`.

Do not write a fake `main` around a portable replacement application. Once `FmApp`'s host declaration is recovered against ViewKit, this exact entrypoint sequence becomes the executable main.
