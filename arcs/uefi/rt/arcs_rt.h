/*
 * ARCS resident runtime: the System Parameter Block (SPB) and firmware vector
 * handed to the operating system, in the layout of IRIX's sys/arcs/spb.h and
 * tvectors.h (reconstructed for x86-64: pointers are 64-bit).
 *
 * Reconstructed platform glue, not SGI source.  Shared by the runtime driver,
 * the PROM application and the Linux kernel console driver (which keeps its
 * own copy of these definitions).
 */
#ifndef ARCS_RT_H
#define ARCS_RT_H

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

#define ARCS_SPBMAGIC		0x53435241	/* SPBMAGIC, "ARCS" */
#define ARCS_RT_VERSION		1
#define ARCS_RT_REVISION	10

/* Firmware vector, in the exact order of IRIX's FirmwareVector.  Each entry is
 * the 64-bit address of an EFIAPI function, or 0 if not provided. */
typedef struct {
    UINT64 Load, Invoke, Execute, Halt, PowerDown, Restart, Reboot;
    UINT64 EnterInteractiveMode, reserved1;
    UINT64 GetPeer, GetChild, GetParent, GetConfigData, AddChild;
    UINT64 DeleteComponent, GetComponent, SaveConfiguration, GetSystemId;
    UINT64 GetMemoryDesc, reserved2, GetTime, GetRelativeTime, GetDirEntry;
    UINT64 Open, Close, Read, GetReadStatus, Write, Seek, Mount;
    UINT64 GetEnvironmentVariable, SetEnvironmentVariable;
    UINT64 GetFileInformation, SetFileInformation, FlushAllCaches;
} ARCS_TV;

/* ARCS Write(): LONG Write(ULONG fd, void *buf, ULONG n, ULONG *cnt).
 * fd 1 is StandardOut. */
typedef INT32 (EFIAPI *ARCS_WRITE)(UINT32 Fd, VOID *Buf, UINT32 N, UINT32 *Cnt);

/* Vendor extension reached through SPB.PrivateVector: where the resident
 * code lives and the console it drives, so the kernel can map both. */
typedef struct {
    UINT32 Magic;			/* ARCS_SPBMAGIC */
    UINT32 Revision;
    UINT64 ImageBase;			/* physical, the whole runtime image */
    UINT64 ImageSize;
    UINT64 FbPhys;			/* physical framebuffer base and size */
    UINT64 FbSize;
    UINT32 FbPitch;			/* bytes per scanline */
    UINT32 FbWidth, FbHeight;
    UINT32 FbPixelFormat;		/* EFI_GRAPHICS_PIXEL_FORMAT */
    UINT32 WinX, WinY, WinW, WinH;	/* console window, pixels */
    UINT32 Cols, Rows;
} ARCS_RT_PRIVATE;

typedef struct {
    UINT32 Signature;			/* ARCS_SPBMAGIC */
    UINT32 Length;
    UINT16 Version;
    UINT16 Revision;
    UINT64 RestartBlock, DebugBlock, GEVector, UTLBMissVector;
    UINT32 TVLength;
    UINT64 TransferVector;		/* physical address of an ARCS_TV */
    UINT32 PTVLength;
    UINT64 PrivateVector;		/* physical address of ARCS_RT_PRIVATE */
} ARCS_SPB;

typedef struct _ARCS_RT_PROTOCOL ARCS_RT_PROTOCOL;

/* Boot-time interface the PROM application uses to set the console up. */
struct _ARCS_RT_PROTOCOL {
    UINT32 Revision;
    ARCS_SPB *Spb;
    EFI_STATUS (EFIAPI *Configure)(ARCS_RT_PROTOCOL *This,
                                   EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
                                   UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                                   UINT32 Fg, UINT32 Bg);	/* colors 0x00RRGGBB */
};

/* "ARCSRT\0\1" and "ARCSSPB\0" in the last bytes. */
#define ARCS_RT_PROTOCOL_GUID \
    { 0x41524353, 0x5254, 0x4f50, { 0x41, 0x52, 0x43, 0x53, 0x52, 0x54, 0x00, 0x01 } }
#define ARCS_SPB_TABLE_GUID \
    { 0x41524353, 0x5350, 0x4200, { 0x41, 0x52, 0x43, 0x53, 0x53, 0x50, 0x42, 0x00 } }

#endif
