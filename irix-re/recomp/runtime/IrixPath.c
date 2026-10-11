#include "IrixGuest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int path_is_within(const char *path, const char *prefix)
{
    size_t n;
    if (!path || !prefix) return 0;
    n = strlen(prefix);
    return strncmp(path, prefix, n) == 0 &&
           (path[n] == '\0' || path[n] == '/');
}

static int path_has_component(const char *path, const char *component)
{
    size_t n = strlen(component);
    return path_is_within(path, component) && path[n] == '/';
}

static int path_is_current_home(const char *path)
{
    const char *home = getenv("HOME");
    return home && home[0] == '/' && path_is_within(path, home);
}

static int path_is_host_virtual(const char *path)
{
    static const char *const roots[] = {
        "/dev", "/proc", "/sys", "/tmp", "/run", "/var/run", 0
    };
    size_t i;
    for (i = 0; roots[i]; ++i)
        if (path_is_within(path, roots[i])) return 1;
    return 0;
}

static int path_is_guest_system(const char *path)
{
    static const char *const roots[] = {
        "/bin", "/sbin", "/lib", "/lib32", "/usr", "/etc",
        "/var", "/opt", "/usr2", 0
    };
    size_t i;
    for (i = 0; roots[i]; ++i)
        if (path_is_within(path, roots[i])) return 1;
    return 0;
}

const char *irix_guest_resolve_path(IrixGuestRuntime *rt, const char *guest_path,
                                    char *buffer, size_t buffer_size,
                                    const char *operation, int optional)
{
    struct stat st;
    const char *resolved = guest_path;

    if (!guest_path) return 0;
    /* IRIX installs /bin/sh as the traditional /sbin/sh image on some
       extracted media. Preserve the guest pathname while resolving the
       installed image from the runtime tree. */
    if (strcmp(guest_path, "/bin/sh") == 0 && rt && rt->rootfs &&
        rt->rootfs[0] && buffer && buffer_size) {
        int n = snprintf(buffer, buffer_size, "%s/sbin/sh", rt->rootfs);
        if (buffer && buffer_size && n > 0 && (size_t)n < buffer_size &&
            stat(buffer, &st) == 0) {
            resolved = buffer;
            goto report;
        }
    }
    if (guest_path[0] == '/' && rt && rt->rootfs && rt->rootfs[0] &&
        !path_is_within(guest_path, rt->rootfs)) {
        int n;

        if (!path_is_host_virtual(guest_path)) {
            /* Real user dotfiles override staged home-directory defaults. */
            if (((path_has_component(guest_path, "/home") ||
                  path_has_component(guest_path, "/usr/people")) &&
                 (path_is_current_home(guest_path) ||
                  stat(guest_path, &st) == 0))) {
                resolved = guest_path;
            } else if (path_is_guest_system(guest_path) ||
                       path_has_component(guest_path, "/home") ||
                       path_has_component(guest_path, "/usr/people")) {
                if (!buffer || buffer_size == 0) return 0;
                n = snprintf(buffer, buffer_size, "%s%s", rt->rootfs,
                             guest_path);
                if (n <= 0 || (size_t)n >= buffer_size) return 0;
                resolved = buffer;
            } else if (stat(guest_path, &st) != 0) {
                if (!buffer || buffer_size == 0) return 0;
                n = snprintf(buffer, buffer_size, "%s%s", rt->rootfs,
                             guest_path);
                if (n <= 0 || (size_t)n >= buffer_size) return 0;
                resolved = buffer;
            }
        }
    }

report:
    (void)operation;
    (void)optional;
    return resolved;
}
