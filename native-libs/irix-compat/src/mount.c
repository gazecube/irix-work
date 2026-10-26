/*
 * IRIX mount(1M) front end.  The boot scripts call IRIX's mount with options
 * that util-linux does not have; this translates them and runs the system
 * mount:
 *   -T types   (IRIX: restrict -a to these types)  ->  -t types
 *   -c         (IRIX: check the filesystem first)   ->  dropped (journaled fs)
 *   -t fd      (/dev/fd)                            ->  nothing to do
 *   -t hwgfs   /hw /hw                              ->  mount -t hwgfs hwgfs /hw
 *   unsupported types in lists (efs, nfs3, nfs3pref, cachefs) are dropped.
 * The real mount is $IRIX_REAL_MOUNT or /usr/bin/mount, /bin/mount.
 * Written for the Linux port.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *drop[] = { "efs", "nfs3", "nfs3pref", "cachefs", "netware", NULL };

/* remove IRIX-only filesystem types from a comma list; "" if none remain */
static char *filter_types(const char *list)
{
	char *out = calloc(1, strlen(list) + 2), *dup = strdup(list), *save, *t;

	for (t = strtok_r(dup, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
		int skip = 0, i;

		for (i = 0; drop[i]; i++)
			if (!strcmp(t, drop[i]))
				skip = 1;
		if (!skip) {
			if (*out)
				strcat(out, ",");
			strcat(out, t);
		}
	}
	free(dup);
	return out;
}

int main(int argc, char **argv)
{
	char *args[argc + 8];
	int n = 0, i, fdtype = 0;
	const char *real = getenv("IRIX_REAL_MOUNT");
	char *tflag = NULL;

	if (!real)
		real = access("/usr/bin/mount", X_OK) == 0 ? "/usr/bin/mount" : "/bin/mount";
	args[n++] = (char *)real;
	for (i = 1; i < argc; i++) {
		char *a = argv[i];

		if (!strcmp(a, "-c") || !strcmp(a, "-v") && 0)
			continue;
		if (!strncmp(a, "-T", 2) || !strncmp(a, "-aT", 3) || !strcmp(a, "-at") ||
		    (!strncmp(a, "-t", 2) && a[2])) {
			/* -T types, -aT types, -Ttypes, -at types, -ttype */
			int all = a[1] == 'a';
			const char *val = a[1] == 'a' ? a + 3 : a + 2;
			char *f;

			if (!*val && i + 1 < argc)
				val = argv[++i];
			if (!strcmp(val, "fd"))
				fdtype = 1;
			f = filter_types(val);
			if (all)
				args[n++] = "-a";
			if (!*f) {			/* nothing left we can mount */
				if (all)
					return 0;
			}
			args[n++] = "-t";
			args[n++] = f;
			tflag = f;
			continue;
		}
		if (!strcmp(a, "-t") && i + 1 < argc) {
			char *f;

			if (!strcmp(argv[i + 1], "fd"))
				fdtype = 1;
			f = filter_types(argv[++i]);
			args[n++] = "-t";
			args[n++] = f;
			tflag = f;
			continue;
		}
		args[n++] = a;
	}
	(void)tflag;
	if (fdtype)
		return 0;			/* /dev/fd is a symlink to /proc/self/fd */
	args[n] = NULL;
	execv(args[0], args);
	perror(args[0]);
	return 127;
}
