from pathlib import Path
p=Path('outputs/mediad-linux/src/mediaclient.c')
s=p.read_text().replace('#include <unistd.h>','#include <unistd.h>\n#include <sys/un.h>\n#include <arpa/inet.h>\n#include <fcntl.h>')
s=s.replace('    int\t\t\tfd;','    int\t\t\tfd;\n    char local_path[108];')
assert 'char local_path' in s
needle='    port->force_unmount\t\t  = 0;'
s=s.replace(needle,needle+'''
    /* Opt-in local transport; preserve portmapper discovery otherwise. */
    if (ipaddr == htonl(INADDR_LOOPBACK) || ipaddr == 0) {
        const char *socket_path = getenv("MEDIAD_SOCKET");
        const char *state = getenv("MEDIAD_STATE");
        int n = 0;
        if (socket_path && *socket_path)
            n = snprintf(port->local_path, sizeof port->local_path, "%s", socket_path);
        else if (state && *state)
            n = snprintf(port->local_path, sizeof port->local_path, "%s/monitor.sock", state);
        if (n >= (int)sizeof port->local_path) { free(port); errno=ENAMETOOLONG; return NULL; }
    }
''')
needle='static void\ntry_to_connect(mc_port_private *port)'
helper='''static void
connected(mc_port_private *port)
{
    port->state = CONNECTED;
    port->whatnext.mcwn_what = MC_INPUT;
    port->whatnext.mcwn_fd = port->fd;
    port->whatnext.mcwn_seconds = 0;
    port->whatnext.mcwn_failcode = MCF_SUCCESS;
    xdrrec_create(&port->xdrs, 100, 100, port, read_proc, write_proc);
    port->xdrs.x_public = (caddr_t)&port->more_xdrs;
    if (port->force_unmount) forced_unmounts(port);
}

'''
s=s.replace(needle,helper+needle)
needle='    assert(port->fd == -1);'
s=s.replace(needle,needle+'''
    if (port->local_path[0]) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof addr);
        addr.sun_family = AF_UNIX;
        strcpy(addr.sun_path, port->local_path);
        fd = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
        if (fd < 0) { try_later(port, MCF_SYS_ERROR); return; }
        rc = connect(fd, (struct sockaddr *)&addr, sizeof addr);
        if (rc == 0) {
            /* The original record reader is blocking after connection. */
            fcntl(fd, F_SETFL, fcntl(fd,F_GETFL)&~O_NONBLOCK);
            port->fd=fd; connected(port); return;
        }
        if (errno == EINPROGRESS) {
            port->fd=fd; port->state=CONNECTING;
            port->whatnext.mcwn_what=MC_OUTPUT;port->whatnext.mcwn_fd=fd;
            port->whatnext.mcwn_failcode=MCF_SUCCESS;return;
        }
        close(fd);try_later(port,MCF_NO_MEDIAD);return;
    }
''')
needle='    rc = connect(port->fd, &port->address, sizeof port->address);'
s=s.replace(needle,'''    if (port->local_path[0]) {
        int error=0; socklen_t size=sizeof error;
        if (getsockopt(port->fd,SOL_SOCKET,SO_ERROR,&error,&size)<0 || error) {
            close(port->fd);port->fd=-1;try_later(port,MCF_NO_MEDIAD);return;
        }
        fcntl(port->fd,F_SETFL,fcntl(port->fd,F_GETFL)&~O_NONBLOCK);
        connected(port);return;
    }
'''+needle)
s=s.replace('sizeof **ppp, xdr_partptr','sizeof **ppp, (xdrproc_t)xdr_partptr')
s=s.replace('connect(fd, &port->address','connect(fd, (struct sockaddr *)&port->address').replace('connect(port->fd, &port->address','connect(port->fd, (struct sockaddr *)&port->address')
s=s.replace('malloc(nv * sizeof *vp)','calloc(nv ? nv : 1, sizeof *vp)').replace('malloc(nd * sizeof *dp)','calloc(nd ? nd : 1, sizeof *dp)').replace('malloc(np * sizeof *pp)','calloc(np ? np : 1, sizeof *pp)')
s=s.replace('    /*\n\t *  Allocate memory for vols, devs and parts.', '    /*\n\t *  Allocate memory for vols, devs and parts.')
# Bound untrusted record counts before allocating and copying arrays.
needle='\t/*\n\t *  Allocate memory for vols, devs and parts. Leave ptrs in sys.\n\t */'
# Actual comment includes two spaces; insert before first allocation instead.
needle='\tmc_volume_t *vp = '
insert='''        if (nv > 65536 || nd > 65536 || np > 65536) {
            xdr_destroy(&port->xdrs);close(port->fd);port->fd=-1;
            try_later(port,MCF_INPUT_ERROR);return;
        }
'''
s=s.replace(needle,insert+needle)
p.write_text(s)
p=Path('outputs/mediad-linux/Makefile');s=p.read_text();s=s.replace('build/fmt_iso.so\n','build/fmt_iso.so build/libmediaclient.so build/libmediaclient.a build/media-client\n',1);s+='''
build/libmediaclient.so: build/mediaclient.o build/xdr_mc.o
	$(CC) -shared -Wl,-soname,libmediaclient.so $^ -ltirpc -o $@
build/libmediaclient.a: build/mediaclient.o build/xdr_mc.o
	$(AR) rcs $@ $^
build/media-client: src/media-client.c build/libmediaclient.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -Lbuild -lmediaclient -ltirpc -Wl,-rpath,'$$ORIGIN' -o $@
''';p.write_text(s)
