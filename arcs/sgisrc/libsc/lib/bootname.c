#ident "lib/libsc/lib/bootname.c:  $Revision: 1.7 $"

/*
 * bootname.c -- parse environment vars to determine bootfile name
 *
 * Original SGI libsc source imported from IRIX 6.5.7m.
 * The include set is reduced for the UEFI standalone build; the function
 * bodies and ARCS environment semantics are preserved.
 */

#define LINESIZE 512

extern int _prom;
extern char *getenv(const char *);
extern char *strcpy(char *, const char *);

static int
arcs_isspace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '\f' || c == '\v';
}

char *env_get_var(char **, char *);

/*
 * make_bootfile - create a reasonable bootfile name
 */
char *
make_bootfile(int rewind)
{
    static char bootfile[LINESIZE];
    static char *sp, *osl;
    static char savesp[128], saveosl[128];
    char spbuf[128], oslbuf[128];
    char *newsp, *newosl;
    char *bp = bootfile;

    /* reset to beginning of environment variable list */
    if (rewind) {
        if (_prom) {
            sp = getenv("SystemPartition");
            osl = getenv("OSLoader");
        }
        else {
            sp = getenv("OSLoadPartition");
            osl = getenv("OSLoadFileName");
        }
        *savesp = '\0';
        *saveosl = '\0';
    }

    /* get the next path in the variable */
    newsp = env_get_var(&sp, spbuf);
    newosl = env_get_var(&osl, oslbuf);

    /* if we've hit the end of all variables, return NULL */
    if (newsp == 0 && newosl == 0)
        return 0;

    /* replace any empty portions of the path with most recent value */
    if (newsp == 0)
        newsp = savesp;
    else
        strcpy(savesp, newsp);

    if (newosl == 0)
        newosl = saveosl;
    else
        strcpy(saveosl, newosl);

    if (newsp) {
        while (*newsp != '\0')
            *bp++ = *newsp++;
        *bp++ = '/';
    }

    if (newosl) {
        while (*newosl != '\0')
            *bp++ = *newosl++;
        *bp = '\0';
    }

    return bootfile;
}

/*
 * env_get_var -- break line into variables
 */
char *
env_get_var(char **cpp, char *buf)
{
    char *wp = buf;
    char c;
    char *cp;

    if (!(cp = *cpp) || *cp == '\0')
        return 0;

    while (arcs_isspace(*cp))
        cp++;

    if (*cp == '\0') {
        *cpp = cp;
        *wp = 0;
        return 0;
    }

    while ((c = *cp) && !arcs_isspace(c)) {
        if (c == ';') {
            cp++;
            break;
        }

        switch (c) {
        case '"':
        case '\'':
            cp++;
            while (*cp && *cp != c)
                *wp++ = *cp++;
            if (*cp == c)
                cp++;
            break;

        default:
            *wp++ = *cp++;
            break;
        }
    }

    while (arcs_isspace(*cp))
        cp++;

    *cpp = cp;
    *wp = 0;
    return buf;
}
