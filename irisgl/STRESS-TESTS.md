# IRIS GL stress-test commands

These are the known-working build invocations for the original SGI IRIX 6.5.7m graphics stress programs in this tree.

Run from the repository root so the `./irix-657m-src/...` paths resolve as written.

```sh
SGI_FLASH=./irix-657m-src/irix/cmd/stress/Gfx/flash.c make flash
```

```sh
SGI_BADIFRED=./irix-657m-src/irix/cmd/stress/Gfx/badifred.c make badifred
```

```sh
SGI_RBUF=./irix-657m-src/irix/cmd/stress/Gfx/rbuf.c make rbuf
```

```sh
SGI_CBUF=./irix-657m-src/irix/cmd/stress/Gfx/cbuf.c make cbuf
```

```sh
SGI_GSYNC=./irix-657m-src/irix/cmd/stress/Gfx/gsync.c make gsync
```

```sh
SGI_MODE=./irix-657m-src/irix/cmd/stress/Gfx/mode.c make mode
```

Current pre-matrix regression baseline:

```text
flash      PASS
badifred   PASS
rbuf       PASS
cbuf       PASS
gsync      PASS
mode       PASS
```

After changing `libgl.a`, rerun this full set before advancing the native 3D path.
