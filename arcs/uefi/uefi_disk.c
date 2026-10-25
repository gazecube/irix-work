/*
 * UEFI Block I/O backend for the original IRIX libsk ARCS device layer.
 *
 * Reconstructed platform glue (not SGI source).  It plays the role of
 * the machine-specific SCSI disk driver (dksc/scsidsk): it registers each
 * UEFI disk in the ARCS configuration tree as scsi(0)disk(N)rdisk(0) and
 * supplies the device strategy routine that the original Open()/Read()/
 * Seek() in libsk/lib/arcsio.c call.  Partition numbers follow IRIX: 0-7
 * and 9 are volume-header slices, 8 is the volume header, 10 the whole disk.
 */
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Protocol/BlockIo.h>

#include <sys/types.h>
#include <arcs/types.h>
#include <arcs/hinv.h>
#include <arcs/folder.h>
#include <arcs/io.h>
#include <arcs/cfgtree.h>
#include <arcs/errno.h>
#include <sys/dvh.h>
#include <sys/dkio.h>
#include <standcfg.h>
#include <libsc.h>
#include <libsk.h>

#define ARCS_UEFI_MAXDISK  16
#define SCSI_DISK_VH_PARTITION 8
#define DVH_BLKS_PER_SEC   1               /* one 512-byte block */

static EFI_BLOCK_IO_PROTOCOL *ArcsDisk[ARCS_UEFI_MAXDISK];
static int ArcsDiskCount;

/* Filesystems linked into the PROM, searched in registration order. */
extern int dvh_install(void);
int (*_fs_table[])(void) = { dvh_install, NULL };

/* config.c expects the platform to provide the tree root and the (here
 * empty) standcfg table. */
COMPONENT cpu_cfgroot = {
    SystemClass, ARC, 0, SGI_ARCS_VERS, SGI_ARCS_REV, 0, 0, 0, 0, 0, 0, NULL
};
struct standcfg standcfg[] = { { NULL, NULL, NULL } };

static cfgnode_t *ArcsRoot;

cfgnode_t *
cpu_makecfgroot(void)
{
    cfgnode_t *n = (cfgnode_t *)malloc(sizeof(cfgnode_t));

    if (!n)
        return NULL;
    bzero(n, sizeof(*n));
    bcopy(&cpu_cfgroot, &n->comp, sizeof(COMPONENT));
    ArcsRoot = n;
    return n;
}

static unsigned int
be32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

/*
 * Find the first block and size of an IRIX partition on this disk.  Reads
 * the volume header (sector 0, big-endian) once per call; returns 0 on
 * success.  Partition 10 (volume) is the whole disk and needs no label.
 */
static int
ArcsDiskPartition(EFI_BLOCK_IO_PROTOCOL *Bio, int Part,
                  unsigned long long *First, unsigned long long *Blocks)
{
    unsigned char *Sec;
    unsigned long long Total = (unsigned long long)Bio->Media->LastBlock + 1;
    EFI_STATUS St;
    unsigned int Magic;
    const unsigned char *Pt;

    if (Part == 10 || Part < 0) {
        *First = 0;
        *Blocks = Total;
        return 0;
    }
    if (Part > 15)
        return -1;

    Sec = AllocatePool(Bio->Media->BlockSize);
    if (!Sec)
        return -1;
    St = Bio->ReadBlocks(Bio, Bio->Media->MediaId, 0, Bio->Media->BlockSize, Sec);
    if (EFI_ERROR(St)) {
        FreePool(Sec);
        return -1;
    }
    Magic = be32(Sec + __builtin_offsetof(struct volume_header, vh_magic));
    if (Magic != VHMAGIC) {
        FreePool(Sec);
        return -1;
    }
    Pt = Sec + __builtin_offsetof(struct volume_header, vh_pt) + Part * sizeof(struct partition_table);
    *Blocks = be32(Pt + __builtin_offsetof(struct partition_table, pt_nblks));
    *First = be32(Pt + __builtin_offsetof(struct partition_table, pt_firstlbn));
    if (Part == SCSI_DISK_VH_PARTITION && *Blocks == 0) {
        /*
         * IRIX always treats slice 8 as the volume header, starting at
         * block 0 and running up to the first partition, even when the
         * label has no explicit entry for it.
         */
        unsigned long long Lowest = 0;
        int k;

        for (k = 0; k < 16; k++) {
            const unsigned char *Q = Sec + __builtin_offsetof(struct volume_header, vh_pt) +
                                     k * sizeof(struct partition_table);
            unsigned long long N = be32(Q + __builtin_offsetof(struct partition_table, pt_nblks));
            unsigned long long F = be32(Q + __builtin_offsetof(struct partition_table, pt_firstlbn));

            if (k == Part || k == 10 || N == 0 || F == 0)
                continue;
            if (Lowest == 0 || F < Lowest)
                Lowest = F;
        }
        *First = 0;
        *Blocks = Lowest ? Lowest : 1;
    }
    FreePool(Sec);
    return (*Blocks == 0) ? -1 : 0;
}

/*
 * Read sector 0 and convert the big-endian on-disk volume header to host
 * order, as the IRIX disk driver hands it to the dvh filesystem (DIOCGETVH).
 * Only the fields dvh.c and the partition code use are converted.
 */
static int
ArcsDiskReadVh(EFI_BLOCK_IO_PROTOCOL *Bio, struct volume_header *Vh)
{
    unsigned char *Sec;
    EFI_STATUS St;
    int k;

    if (Bio->Media->BlockSize != BLKSIZE)
        return -1;
    Sec = AllocatePool(BLKSIZE);
    if (!Sec)
        return -1;
    St = Bio->ReadBlocks(Bio, Bio->Media->MediaId, 0, BLKSIZE, Sec);
    if (EFI_ERROR(St) || be32(Sec + __builtin_offsetof(struct volume_header, vh_magic)) != VHMAGIC) {
        FreePool(Sec);
        return -1;
    }
    CopyMem(Vh, Sec, sizeof(*Vh));
    Vh->vh_magic = be32(Sec + __builtin_offsetof(struct volume_header, vh_magic));
    Vh->vh_rootpt = (short)((Sec[4] << 8) | Sec[5]);
    Vh->vh_swappt = (short)((Sec[6] << 8) | Sec[7]);
    Vh->vh_csum = be32(Sec + __builtin_offsetof(struct volume_header, vh_csum));
    for (k = 0; k < NVDIR; k++) {
        const unsigned char *Q = Sec + __builtin_offsetof(struct volume_header, vh_vd) +
                                 k * sizeof(struct volume_directory);
        Vh->vh_vd[k].vd_lbn = be32(Q + __builtin_offsetof(struct volume_directory, vd_lbn));
        Vh->vh_vd[k].vd_nbytes = be32(Q + __builtin_offsetof(struct volume_directory, vd_nbytes));
    }
    for (k = 0; k < NPARTAB; k++) {
        const unsigned char *Q = Sec + __builtin_offsetof(struct volume_header, vh_pt) +
                                 k * sizeof(struct partition_table);
        Vh->vh_pt[k].pt_nblks = be32(Q + __builtin_offsetof(struct partition_table, pt_nblks));
        Vh->vh_pt[k].pt_firstlbn = be32(Q + __builtin_offsetof(struct partition_table, pt_firstlbn));
        Vh->vh_pt[k].pt_type = be32(Q + __builtin_offsetof(struct partition_table, pt_type));
    }
    FreePool(Sec);
    return 0;
}

/* Per-open state kept in iob->DevPtr. */
struct ArcsDiskOpen {
    EFI_BLOCK_IO_PROTOCOL *Bio;
    unsigned long long First;
    unsigned long long Blocks;
};

STATUS ArcsUefiDiskStrat(COMPONENT *Self, IOBLOCK *Iob);

STATUS
ArcsUefiDiskStrat(COMPONENT *Self, IOBLOCK *Iob)
{
    struct ArcsDiskOpen *D;
    EFI_STATUS St;
    unsigned long long Lba, Nblk;

    (void)Self;
    switch (Iob->FunctionCode) {
    case FC_INITIALIZE:
        return ESUCCESS;

    case FC_OPEN:
        /* parse_path(): Controller = scsi adapter key, Unit = disk key. */
        if (Iob->Controller != 0 || Iob->Unit < 0 ||
            Iob->Unit >= ArcsDiskCount)
            return Iob->ErrorNumber = ENXIO;
        D = AllocatePool(sizeof(*D));
        if (!D)
            return Iob->ErrorNumber = ENOMEM;
        D->Bio = ArcsDisk[Iob->Unit];
        if (ArcsDiskPartition(D->Bio, (int)Iob->Partition, &D->First,
                              &D->Blocks)) {
            FreePool(D);
            return Iob->ErrorNumber = ENXIO;
        }
        Iob->DevPtr = D;
        return ESUCCESS;

    case FC_CLOSE:
        if (Iob->DevPtr)
            FreePool(Iob->DevPtr);
        Iob->DevPtr = NULL;
        return ESUCCESS;

    case FC_READ:
    case FC_WRITE:
        D = (struct ArcsDiskOpen *)Iob->DevPtr;
        if (!D)
            return Iob->ErrorNumber = EINVAL;
        /* The ARCS block layer works in 512-byte blocks (BLKSIZE). */
        if (D->Bio->Media->BlockSize != BLKSIZE)
            return Iob->ErrorNumber = EIO;
        Lba = (unsigned long long)Iob->StartBlock;
        Nblk = Iob->Count / BLKSIZE;
        if (Lba + Nblk > D->Blocks)
            return Iob->ErrorNumber = EIO;
        if (Iob->FunctionCode == FC_READ)
            St = D->Bio->ReadBlocks(D->Bio, D->Bio->Media->MediaId,
                                    D->First + Lba, Nblk * BLKSIZE, Iob->Address);
        else if (D->Bio->Media->ReadOnly)
            return Iob->ErrorNumber = EROFS;
        else
            St = D->Bio->WriteBlocks(D->Bio, D->Bio->Media->MediaId,
                                     D->First + Lba, Nblk * BLKSIZE, Iob->Address);
        return EFI_ERROR(St) ? (Iob->ErrorNumber = EIO) : ESUCCESS;

    case FC_GETREADSTATUS:
        return ESUCCESS;

    case FC_IOCTL:
        D = (struct ArcsDiskOpen *)Iob->DevPtr;
        if (!D)
            return Iob->ErrorNumber = EINVAL;
        switch ((unsigned long)Iob->IoctlCmd) {
        case DIOCSETBLKSZ:
            /* ARCS block I/O is fixed at BLKSIZE (512) here. */
            return (D->Bio->Media->BlockSize == BLKSIZE) ? ESUCCESS
                                                          : (Iob->ErrorNumber = EIO);
        case DIOCGETVH:
            if (ArcsDiskReadVh(D->Bio, (struct volume_header *)Iob->IoctlArg))
                return Iob->ErrorNumber = EIO;
            return ESUCCESS;
        default:
            return Iob->ErrorNumber = EINVAL;
        }

    default:
        return Iob->ErrorNumber = EINVAL;
    }
}

/*
 * Enumerate UEFI whole-disk Block I/O handles and register
 * scsi(0)disk(N)rdisk(0) for each in the ARCS configuration tree.
 */
int
ArcsUefiDiskInit(void)
{
    EFI_HANDLE *H;
    UINTN N, i;
    COMPONENT *Root, *Adap, *Ctlr, *Peri;
    COMPONENT T;

    fs_init();
    init_cfgtree();
    Root = (COMPONENT *)ArcsRoot;
    if (!Root)
        return 0;

    if (EFI_ERROR(gBS->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid,
                                          NULL, &N, &H)))
        return 0;

    bzero(&T, sizeof(T));
    T.Class = AdapterClass;
    T.Type = SCSIAdapter;
    T.Version = SGI_ARCS_VERS;
    T.Revision = SGI_ARCS_REV;
    Adap = AddChild(Root, &T, NULL);
    if (!Adap)
        return 0;

    for (i = 0; i < N && ArcsDiskCount < ARCS_UEFI_MAXDISK; i++) {
        EFI_BLOCK_IO_PROTOCOL *Bio;

        if (EFI_ERROR(gBS->HandleProtocol(H[i], &gEfiBlockIoProtocolGuid,
                                          (VOID **)&Bio)))
            continue;
        if (!Bio->Media || Bio->Media->LogicalPartition ||
            !Bio->Media->MediaPresent)
            continue;

        bzero(&T, sizeof(T));
        T.Class = ControllerClass;
        T.Type = DiskController;
        T.Key = ArcsDiskCount;
        T.Flags = (IDENTIFIERFLAG)(Input | Output);
        T.Version = SGI_ARCS_VERS;
        T.Revision = SGI_ARCS_REV;
        Ctlr = AddChild(Adap, &T, NULL);
        if (!Ctlr)
            continue;

        bzero(&T, sizeof(T));
        T.Class = PeripheralClass;
        T.Type = DiskPeripheral;
        T.Key = 0;
        T.Flags = (IDENTIFIERFLAG)(Input | Output);
        if (Bio->Media->ReadOnly)
            T.Flags = (IDENTIFIERFLAG)Input;
        T.Version = SGI_ARCS_VERS;
        T.Revision = SGI_ARCS_REV;
        Peri = AddChild(Ctlr, &T, NULL);
        if (!Peri)
            continue;
        RegisterDriverStrategy(Peri, ArcsUefiDiskStrat);
        ArcsDisk[ArcsDiskCount++] = Bio;
    }
    FreePool(H);
    return ArcsDiskCount;
}

/* ---- bring-up self test: exercises the original Open/Read/Close path ---- */

static void
ArcsDiskLog(const CHAR16 *Msg)
{
    gST->ConOut->OutputString(gST->ConOut, (CHAR16 *)L"ARCSDISK: ");
    gST->ConOut->OutputString(gST->ConOut, (CHAR16 *)Msg);
    gST->ConOut->OutputString(gST->ConOut, (CHAR16 *)L"\r\n");
}

static void
ArcsDiskLogHex(const CHAR16 *Label, unsigned long long V)
{
    CHAR16 B[40];
    int i;

    for (i = 0; i < 16; i++)
        B[i] = L"0123456789abcdef"[(V >> (60 - 4 * i)) & 0xf];
    B[16] = 0;
    gST->ConOut->OutputString(gST->ConOut, (CHAR16 *)L"ARCSDISK: ");
    gST->ConOut->OutputString(gST->ConOut, (CHAR16 *)Label);
    gST->ConOut->OutputString(gST->ConOut, (CHAR16 *)L" 0x");
    gST->ConOut->OutputString(gST->ConOut, B);
    gST->ConOut->OutputString(gST->ConOut, (CHAR16 *)L"\r\n");
}

void
ArcsUefiDiskSelfTest(void)
{
    static const int Parts[] = { 10, 8, 0, 1 };
    unsigned char Buf[BLKSIZE];
    char Path[64];
    ULONG Fd, Got;
    LONG Rc;
    int d, i;

    ArcsDiskLogHex(L"disks found", (unsigned long long)ArcsDiskCount);
    for (d = 0; d < ArcsDiskCount; d++) {
        AsciiSPrint(Path, sizeof(Path), "scsi(0)disk(%d)rdisk(0)partition(8)hello", d);
        Rc = Open(Path, OpenReadOnly, &Fd);
        ArcsDiskLogHex(L"vh file open rc", (unsigned long long)(long long)Rc);
        if (Rc == ESUCCESS) {
            Got = 0;
            Rc = Read(Fd, Buf, 16, &Got);
            ArcsDiskLogHex(L"  read rc", (unsigned long long)(long long)Rc);
            ArcsDiskLogHex(L"  bytes", Got);
            Buf[15] = 0;
            ArcsDiskLogHex(L"  text0", ((unsigned long long)Buf[0] << 24) | (Buf[1] << 16) | (Buf[2] << 8) | Buf[3]);
            Close(Fd);
        }
    }
    for (d = 0; d < ArcsDiskCount; d++) {
        ArcsDiskLogHex(L"disk", (unsigned long long)d);
        ArcsDiskLogHex(L"  blocks", (unsigned long long)ArcsDisk[d]->Media->LastBlock + 1);
        for (i = 0; i < 4; i++) {
            AsciiSPrint(Path, sizeof(Path), "scsi(0)disk(%d)rdisk(0)partition(%d)", d, Parts[i]);
            ArcsDiskLogHex(L"  partition", (unsigned long long)Parts[i]);
            Rc = Open(Path, OpenReadOnly, &Fd);
            ArcsDiskLogHex(L"    Open rc", (unsigned long long)(long long)Rc);
            if (Rc != ESUCCESS)
                continue;
            Got = 0;
            Rc = Read(Fd, Buf, BLKSIZE, &Got);
            ArcsDiskLogHex(L"    Read rc", (unsigned long long)(long long)Rc);
            ArcsDiskLogHex(L"    bytes", Got);
            ArcsDiskLogHex(L"    first word", ((unsigned long long)Buf[0] << 24) |
                           (Buf[1] << 16) | (Buf[2] << 8) | Buf[3]);
            Close(Fd);
        }
    }
}
