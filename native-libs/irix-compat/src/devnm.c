/*
 * devnm: print the name of the device a file is on (IRIX devnm(1M)).
 * Original IRIX output: "<device> <file>", the device found by matching the
 * file's st_dev against the block devices in /dev/dsk, then /dev.
 * Written for the Linux port; IRIX does not ship the source.
 */
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

static int find(const char *dir, dev_t dev, char *out, size_t n)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	struct stat st;
	char path[1024];

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		if (stat(path, &st) == 0 && S_ISBLK(st.st_mode) && st.st_rdev == dev) {
			snprintf(out, n, "%s", path);
			closedir(d);
			return 1;
		}
	}
	closedir(d);
	return 0;
}

int main(int argc, char **argv)
{
	int i, rc = 0;

	if (argc < 2) {
		fprintf(stderr, "usage: devnm name ...\n");
		return 1;
	}
	for (i = 1; i < argc; i++) {
		struct stat st;
		char dev[1024];

		if (stat(argv[i], &st) < 0) {
			perror(argv[i]);
			rc = 1;
			continue;
		}
		if (!find("/dev/dsk", st.st_dev, dev, sizeof(dev)) && !find("/dev", st.st_dev, dev, sizeof(dev)))
			snprintf(dev, sizeof(dev), "%u,%u", major(st.st_dev), minor(st.st_dev));
		printf("%s %s\n", dev, argv[i]);
	}
	return rc;
}
