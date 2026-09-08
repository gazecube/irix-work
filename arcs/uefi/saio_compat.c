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

/*
 * env_cmd.c uses the original ARCS Open()/Close() interface only to reopen
 * serial(0)/serial(1) after dbaud/rbaud changes.  Native UEFI serial-device
 * plumbing is not part of the current console implementation, so report the
 * original ARCS ENODEV result rather than inventing a descriptor.  This keeps
 * the pristine command path intact and makes those optional reopen blocks
 * simply not execute until the serial backend is ported.
 */
LONG
Open(CHAR *Path, OPENMODE Mode, ULONG *FileId)
{
    (void)Path;
    (void)Mode;
    if (FileId)
        *FileId = 0;
    return ENODEV;
}

LONG
Close(ULONG FileId)
{
    (void)FileId;
    return ESUCCESS;
}
