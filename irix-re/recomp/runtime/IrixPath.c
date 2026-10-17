#include "IrixGuest.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
        "/dev", "/proc", "/sys", "/tmp", "/var/tmp", "/run", "/var/run", 0
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

/* realpath() inside the guest namespace.  Each component is looked up through
   the guest-to-host path mapping, and absolute symlink targets are mapped as
   guest paths too; the host realpath() would follow them through the host
   root (merged /usr/sbin -> bin, host-only files) and return paths the guest
   tree does not contain. */
int irix_guest_realpath(IrixGuestRuntime *rt, const char *path, char *out)
{
    char work[PATH_MAX], cur[PATH_MAX], probe[PATH_MAX], host[PATH_MAX];
    char link[PATH_MAX];
    size_t olen = 1u;
    unsigned links = 0;
    struct stat st;
    const char *p;

    if (!path || path[0] != '/' || strlen(path) >= sizeof(work)) return -1;
    strcpy(work, path);
    strcpy(cur, "/");
    p = work;
    while (*p) {
        const char *e;
        size_t clen;
        while (*p == '/') ++p;
        if (!*p) break;
        e = p;
        while (*e && *e != '/') ++e;
        clen = (size_t)(e - p);
        if (clen == 1u && p[0] == '.') { p = e; continue; }
        if (clen == 2u && p[0] == '.' && p[1] == '.') {
            while (olen > 1u && cur[olen - 1u] != '/') --olen;
            if (olen > 1u) --olen;
            cur[olen] = '\0';
            if (olen == 0u) { cur[0] = '/'; cur[1] = '\0'; olen = 1u; }
            p = e;
            continue;
        }
        if (olen + clen + 2u >= sizeof(cur)) { errno = ENAMETOOLONG; return -1; }
        if (olen > 1u) cur[olen++] = '/';
        memcpy(cur + olen, p, clen);
        olen += clen;
        cur[olen] = '\0';
        p = e;
        {
            const char *m = irix_guest_resolve_path(rt, cur, host, sizeof(host),
                                                    "realpath", 0);
            ssize_t ln;
            if (!m || lstat(m, &st) != 0) return -1;
            if (!S_ISLNK(st.st_mode)) continue;
            if (++links > 40u) { errno = ELOOP; return -1; }
            ln = readlink(m, link, sizeof(link) - 1u);
            if (ln <= 0) return -1;
            link[ln] = '\0';
            if (snprintf(probe, sizeof(probe), "%s%s%s", link,
                         *p ? "/" : "", p) >= (int)sizeof(probe)) {
                errno = ENAMETOOLONG;
                return -1;
            }
            if (link[0] == '/') {
                strcpy(cur, "/"); olen = 1u;
            } else {
                while (olen > 1u && cur[olen - 1u] != '/') --olen;
                if (olen > 1u) --olen;
                cur[olen] = '\0';
                if (olen == 0u) { cur[0] = '/'; cur[1] = '\0'; olen = 1u; }
            }
            strcpy(work, probe);
            p = work;
        }
    }
    strcpy(out, cur);
    return 0;
}

/* An absolute symlink target means a guest path; the host kernel would follow
   it through the host root.  For open(), resolve such links in the guest
   namespace so e.g. a link to /usr/sbin/fm reaches the tree's own image. */
const char *irix_guest_follow_link(IrixGuestRuntime *rt,
                                          const char *host, char *buffer,
                                          size_t buffer_size)
{
    struct stat st;
    char target[PATH_MAX], real[PATH_MAX];
    size_t n;
    ssize_t ln;
    const char *guest;

    if (!rt || !rt->rootfs || !rt->rootfs[0] || !host) return host;
    if (lstat(host, &st) != 0 || !S_ISLNK(st.st_mode)) return host;
    ln = readlink(host, target, sizeof(target) - 1u);
    if (ln <= 0) return host;
    target[ln] = '\0';
    if (target[0] != '/') return host;
    n = strlen(rt->rootfs);
    if (strncmp(host, rt->rootfs, n) != 0 || host[n] != '/') return host;
    guest = host + n;
    if (irix_guest_realpath(rt, guest, real) != 0) return host;
    if (!buffer || !buffer_size) return host;
    return irix_guest_resolve_path(rt, real, buffer, buffer_size,
                                   "realpath", 0);
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
    (void)optional;
    if (operation && strcmp(operation, "open") == 0 && resolved != guest_path &&
        buffer && buffer_size) {
        char follow[PATH_MAX];
        const char *f = irix_guest_follow_link(rt, resolved, follow,
                                               sizeof(follow));
        if (f != resolved && strlen(f) < buffer_size) {
            if (f != buffer) memmove(buffer, f, strlen(f) + 1u);
            resolved = buffer;
        }
    }
    return resolved;
}
