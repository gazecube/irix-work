/*
 * fsstat special: filesystem state, with IRIX's exit codes.
 *   0  unmounted and okay (or the mounted root is in a journaled filesystem)
 *   1  unmounted and needs checking
 *   2  mounted
 *   3  error
 * IRIX's version reads EFS/XFS superblocks; here XFS, btrfs and f2fs are
 * journaled/log-structured (okay), ext2/3/4 report their state flag, and the
 * root filesystem is never "dirty" since a mounted root cannot be checked.
 * Written for the Linux port.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/sysmacros.h>

static int is_mounted(dev_t rdev)
{
	FILE *f = fopen("/proc/self/mountinfo", "r");
	char line[1024];
	unsigned ma, mi;

	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%*d %*d %u:%u", &ma, &mi) == 2 && makedev(ma, mi) == rdev) {
			fclose(f);
			return 1;
		}
	}
	fclose(f);
	return 0;
}

int main(int argc, char **argv)
{
	struct stat sd, sr;
	unsigned char sb[2048];
	int fd;
	long n;

	if (argc != 2) {
		fprintf(stderr, "usage: fsstat special\n");
		return 3;
	}
	if (stat(argv[1], &sd) < 0) {
		fprintf(stderr, "fsstat: cannot stat %s\n", argv[1]);
		return 3;
	}
	if (!S_ISBLK(sd.st_mode) && !S_ISCHR(sd.st_mode)) {
		fprintf(stderr, "fsstat: %s is not a device\n", argv[1]);
		return 3;
	}
	if (stat("/", &sr) == 0 && sr.st_dev == sd.st_rdev) {
		fprintf(stderr, "fsstat: root file system okay\n");
		return 0;
	}
	if (is_mounted(sd.st_rdev)) {
		fprintf(stderr, "fsstat: %s mounted\n", argv[1]);
		return 2;
	}
	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "fsstat: cannot read %s\n", argv[1]);
		return 3;
	}
	n = pread(fd, sb, sizeof(sb), 0);
	if (n < 2048) {
		fprintf(stderr, "fsstat: cannot read %s\n", argv[1]);
		return 3;
	}
	if (!memcmp(sb, "XFSB", 4)) {			/* XFS: log replay on mount */
		fprintf(stderr, "fsstat: %s okay\n", argv[1]);
		return 0;
	}
	if (sb[1080] == 0x53 && sb[1081] == 0xEF) {	/* ext2/3/4 */
		unsigned state = sb[1082] | sb[1083] << 8;

		if (state & 1) {
			fprintf(stderr, "fsstat: %s okay\n", argv[1]);
			return 0;
		}
		fprintf(stderr, "fsstat: %s needs checking\n", argv[1]);
		return 1;
	}
	fprintf(stderr, "fsstat: %s okay\n", argv[1]);	/* unknown: nothing we can check */
	return 0;
}
