#include <arcs/types.h>
#include <arcs/eiob.h>
#include <saio.h>
#include <libsc.h>

/*
 * Standalone I/O control block table.  new_eiob/get_eiob/free_eiob follow
 * the original IRIX libsk saio.c; the whole of saio.c is not used because
 * its initialisation depends on the MIPS PROM environment.  Descriptors 0
 * and 1 are the console and are not allocated here.
 */
static struct eiob *_eiobp[ARCS_FOPEN_MAX];

struct eiob *
new_eiob(void)
{
    int i;

    for (i = 2; i < ARCS_FOPEN_MAX; i++)
        if (_eiobp[i] == 0)
            break;

    if ((i == ARCS_FOPEN_MAX) ||
        !(_eiobp[i] = (struct eiob *)malloc(sizeof *_eiobp[0])))
        return(NULL);

    bzero(_eiobp[i], sizeof *_eiobp[i]);
    _eiobp[i]->fd = i;

    return _eiobp[i];
}

struct eiob *
get_eiob(ULONG fd)
{
    if (fd >= ARCS_FOPEN_MAX)
        return NULL;
    return _eiobp[fd];
}

void
free_eiob(struct eiob *io)
{
    if (io->filename) free(io->filename);
    _eiobp[io->fd] = 0;
    free(io);
}

/*
 * saio.c polls every open console-class device for input between reads.
 * The UEFI console is polled by Read() itself, so there is nothing to do.
 */
void
_scandevs(void)
{
}

/*
 * Block I/O buffers for filesystem drivers (saio.c _get_iobbuf).  Sized
 * BLKSIZE*2 as in the original; the PROM heap stands in for the MIPS DMA
 * buffer pool.
 */
char *
_get_iobbuf(void)
{
    char *cp = (char *)malloc(BLKSIZE * 2);

    if (cp)
        return cp;
    panic("out of io buffers");
    return NULL;
}

void
_free_iobbuf(char *cp)
{
    free(cp);
}

/* libsk debug level flag (libsk/lib/skutils.c on the original). */
int Debug;
