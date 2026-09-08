/*
 * Source-derived port of edit_gets() from IRIX 6.5.7m:
 *   stand/arcs/lib/libsc/lib/stdio.c, Revision 1.65
 *
 * Kept separate from stdio.c while the rest of standalone stdio is ported.
 * The SPB debugger hook is omitted at the x86-64/UEFI machine boundary;
 * command editing/history behavior is otherwise retained.
 */

#include <sys/types.h>
#include <ctype.h>
#include <arcs/io.h>
#include <libsc.h>
#include <parser.h>
#include <saioctl.h>

#define CTRL(x)         ((x)&0x1f)
#define DEL             0x7f
#define BELL            0x7

char *
edit_gets(struct edit_linebuf *elinebuf)
{
    char *buf = elinebuf->lines[elinebuf->current];
    char *bufp, *bufc, *tmpp, *rvalp=NULL;
    int len = LINESIZE;
    int current = elinebuf->current;
    ULONG fd = 0;
    int tmp;
    int c;

    bufp = bufc = buf;

    ioctl(fd,TIOCRAW,1);

    for (;;) {
        c = getc(fd);

        switch (c) {
        case CTRL('N'):
            if (current != elinebuf->current) {
                tmp = (current + 1) % EDIT_HISTORY;
                if (elinebuf->lines[tmp][0] != '\0' ||
                    tmp == elinebuf->current) {
                    current = tmp;
                    goto newbuffer;
                }
            }
            putchar(BELL);
            break;

        case CTRL('P'):
            tmp = current - 1;
            if (tmp < 0) tmp = EDIT_HISTORY - 1;
            if (tmp != elinebuf->current) {
                if (elinebuf->lines[tmp][0] != '\0') {
                    current = tmp;
                    goto newbuffer;
                }
            }
            putchar(BELL);
            break;

        case CTRL('V'):
            c = getc(fd);
            if (bufp < &buf[len-3]) {
                *bufp++ = (char)c;
                bufc++;
                showchar(c);
            } else
                putchar(BELL);
            break;

        case CTRL('D'):
        case CTRL('X'):
        case EOF:
            if (bufc != bufp) {
                bcopy(bufc+1,bufc,bufp-bufc);
                *--bufp = '\0';
                printf("%s ",bufc);
                tmpp = bufp;
                while (tmpp >= bufc) {
                    putchar(CTRL('H'));
                    tmpp--;
                }
            }
            else if (bufp == buf)
                EnterInteractiveMode();
            break;

        case '\n':
        case '\r':
            putchar('\n');
            *bufp = '\0';
            rvalp = buf;
            break;

        case CTRL('H'):
        case DEL:
            if (bufc > buf) {
                if (bufc == bufp) {
                    *--bufp = 0;
                    bufc--;
                    putchar(CTRL('H'));
                    putchar(' ');
                    putchar(CTRL('H'));
                }
                else {
                    putchar(CTRL('H'));
                    bcopy(bufc,bufc-1,bufp-bufc);
                    bufc--;
                    *--bufp = '\0';
                    printf("%s ",bufc);
                    tmpp = bufp;
                    while (tmpp >= bufc) {
                        putchar(CTRL('H'));
                        tmpp--;
                    }
                }
            }
            break;

        case CTRL('E'):
            while (bufc < bufp) {
                putchar(*bufc);
                bufc++;
            }
            break;

        case CTRL('A'):
            while (bufc > buf) {
                putchar(CTRL('H'));
                bufc--;
            }
            break;

        case CTRL('F'):
            if (bufc < bufp) {
                putchar(*bufc);
                bufc++;
            }
            break;

        case CTRL('B'):
            if (bufc > buf) {
                bufc--;
                putchar(CTRL('H'));
            }
            break;

        case CTRL('K'):
            if (bufc != bufp) {
                tmpp = bufc;
                while (tmpp < bufp) {
                    putchar(' ');
                    tmpp++;
                }
                while (bufc < bufp) {
                    putchar(CTRL('H'));
                    *bufp = '\0';
                    bufp--;
                }
            }
            break;

        case CTRL('R'):
            if ((bufp-buf) > 65) {
                printf("^R\n? ");
            }
            else {
                while (bufc < bufp) {
                    putchar(*bufc);
                    bufc++;
                }
                bufc = bufp;
                while (bufc > buf) {
                    putchar(CTRL('H'));
                    putchar(' ');
                    putchar(CTRL('H'));
                    bufc--;
                }
            }
            *bufp = '\0';
            bufc = bufp;
            printf("%s",buf);
            break;

        case CTRL('U'):
            while (bufc < bufp) {
                putchar(*bufc);
                bufc++;
            }
            while (bufp-- > buf) {
                putchar(CTRL('H'));
                putchar(' ');
                putchar(CTRL('H'));
            }
            bufp = bufc = buf;
            break;

        case '\t':
            c = ' ';
            /*FALLSTHROUGH*/

        default:
            if (isprint(c) && bufp < &buf[len-3]) {
                putchar(c);

                if (bufc != bufp)
                    bcopy(bufc,bufc+1,bufp-bufc);

                *bufc++ = (char)c;
                bufp++;

                if (bufc != bufp) {
                    *bufp = '\0';
                    printf("%s",bufc);
                    tmpp = bufp;
                    while (tmpp > bufc) {
                        putchar(CTRL('H'));
                        tmpp--;
                    }
                }
            } else
                putchar(BELL);
            break;
newbuffer:
            while (bufc < bufp) {
                putchar(*bufc);
                bufc++;
            }
            while (bufp-- > buf) {
                putchar(CTRL('H'));
                putchar(' ');
                putchar(CTRL('H'));
            }
            buf = elinebuf->lines[current];
            bufp = buf + strlen(buf);
            *bufp = '\0';
            bufc = bufp;
            printf("%s",buf);
            break;
        }

        if (rvalp)
            break;
    }

    ioctl(fd,TIOCRAW,0);

    if (rvalp && rvalp[0] == '!') {
        tmp = elinebuf->current;

        if (rvalp[1] == '!' && rvalp[2] == '\0') {
            if (--tmp < 0) tmp = EDIT_HISTORY - 1;
        }

        if (tmp != elinebuf->current &&
            elinebuf->lines[tmp][0] != '\0') {
            current = tmp;
            printf("%s\n",elinebuf->lines[current]);
        }
    }

    if (rvalp && *rvalp != '\0') {
        if (current != elinebuf->current) {
            bcopy(elinebuf->lines[current],
                  elinebuf->lines[elinebuf->current],LINESIZE);
        }
        elinebuf->current = (elinebuf->current + 1) % EDIT_HISTORY;
        elinebuf->lines[elinebuf->current][0] = '\0';
    }

    return(rvalp);
}
