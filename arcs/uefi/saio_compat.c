#include <arcs/io.h>
#include <arcs/errno.h>
#include <libsc.h>
#include <saioctl.h>

/*
 * Native UEFI backing for the standalone tty ioctls used by pristine libsc.
 *
 * SGI's libsc isgraphic() asks the underlying standalone I/O layer whether
 * the descriptor is a DisplayController via TIOCISGRAPHIC.  On this port the
 * ARCS console is the GOP-backed graphical console, so preserve that contract
 * here rather than hard-wiring isgraphic() itself.
 */
LONG
ioctl(ULONG fd, LONG cmd, LONG arg)
{
    switch (cmd) {
    case TIOCISGRAPHIC:
        if (arg)
            *((int *)(unsigned long)arg) =
                (fd == StandardIn || fd == StandardOut) ? 1 : 0;
        return 0;

    case TIOCISATTY:
        if (arg)
            *((int *)(unsigned long)arg) =
                (fd == StandardIn || fd == StandardOut) ? 1 : 0;
        return 0;

    case TIOCRAW:
    case TIOCRRAW:
    case TIOCSETXOFF:
    case TIOCCHECKSTOP:
    case TIOCFLUSH:
        /* UEFI SimpleTextInput does not expose an equivalent line discipline.
         * These are successful no-ops at the platform boundary. */
        return 0;

    default:
        return -1;
    }
}

/* Original stand/arcs/lib/libsc/lib/scsaio.c implementation. */
int
isgraphic(ULONG fd)
{
    int arg;

    ioctl(fd, TIOCISGRAPHIC, (LONG)(unsigned long)&arg);
    return arg;
}

extern LONG ArcsOpen(CHAR *, OPENMODE, ULONG *);
extern LONG ArcsClose(ULONG);

/* Console descriptors are native; everything else is the original ARCS
 * Open/Close in libsk (arcsio.c) over the registered device components. */
LONG
Open(CHAR *Path, OPENMODE Mode, ULONG *FileId)
{
    return ArcsOpen(Path, Mode, FileId);
}

LONG
Close(ULONG FileId)
{
    if (FileId <= StandardOut)
        return ESUCCESS;
    return ArcsClose(FileId);
}

/* MIPS write-buffer flush: x86 has strongly ordered stores for this use. */
void
wbflush(void)
{
}
