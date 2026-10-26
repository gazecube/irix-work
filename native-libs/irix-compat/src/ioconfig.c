/*
 * ioconfig [-f] [-d] [-v] [hwgraph mount point]
 *
 * IRIX's ioconfig walks the hardware graph and asks the drivers to create the
 * /dev/dsk, /dev/rdsk, /dev/rmt, /dev/tty aliases.  On this system the kernel
 * (patch 0001) and udev already build those names, so ioconfig only makes sure
 * the hardware graph filesystem is mounted at /hw and that the disk aliases
 * exist.  Exit status is 0 when the aliases are in place.
 * Written for the Linux port.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int is_hwgfs(const char *dir)
{
	char p[512];
	struct stat st;

	snprintf(p, sizeof(p), "%s/.devhdl", dir);	/* the marker mnthwgfs tests */
	return stat(p, &st) == 0;
}

static int run(char *const argv[])
{
	pid_t pid = fork();
	int st;

	if (pid == 0) {
		execv(argv[0], argv);
		_exit(127);
	}
	if (pid < 0 || waitpid(pid, &st, 0) < 0)
		return -1;
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int main(int argc, char **argv)
{
	const char *hw = "/hw";
	int c, verbose = 0, force = 0;
	DIR *d;
	int disks = 0;
	struct dirent *e;

	while ((c = getopt(argc, argv, "fdvs")) != -1) {
		switch (c) {
		case 'v': verbose = 1; break;
		case 'f': force = 1; break;
		case 'd': case 's': break;
		default:
			fprintf(stderr, "usage: ioconfig [-fdvs] [hw-mountpoint]\n");
			return 1;
		}
	}
	if (optind < argc)
		hw = argv[optind];
	(void)force;

	if (!is_hwgfs(hw) && access(hw, F_OK) == 0) {
		char *m[] = { "/sbin/mount", "-t", "hwgfs", (char *)hw, (char *)hw, NULL };

		if (run(m) != 0 && verbose)
			fprintf(stderr, "ioconfig: cannot mount hwgfs at %s\n", hw);
	}
	d = opendir("/dev/dsk");
	if (d) {
		while ((e = readdir(d)))
			if (!strncmp(e->d_name, "dks", 3))
				disks++;
		closedir(d);
	}
	if (verbose)
		printf("ioconfig: %s %s, %d disk aliases in /dev/dsk\n", hw,
		       is_hwgfs(hw) ? "mounted" : "not mounted", disks);
	return disks ? 0 : 1;
}
