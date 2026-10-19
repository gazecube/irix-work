/* Linux adaptation for mediad's narrow FAM interface. Watches parent
 * directories so atomic file replacement continues to deliver changes. */
#include <fam.h>
#include <sys/inotify.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <errno.h>
#include <deque>
#include <vector>
#include <string>
#include <map>
#include <stdlib.h>
#include <string.h>
extern "C" { int dsdebug; }
struct Watch { int wd, req; std::string name, path; bool directory; };
struct FamLinux {
    int in, notify;
    std::vector<Watch> watches;
    std::deque<FAMEvent> pending;
};
static void queue(FAMConnection *fc, const Watch& w,FAMCodes code) {
    FamLinux *f=(FamLinux *)fc->client;FAMEvent e;
    memset(&e,0,sizeof e);e.fc=fc;e.fr.reqnum=w.req;e.code=code;
    strncpy(e.filename,w.path.c_str(),sizeof e.filename-1);f->pending.push_back(e);
    uint64_t one=1; (void)write(f->notify,&one,sizeof one);
}
extern "C" int FAMOpen2(FAMConnection *fc,const char *) {
    FamLinux *f=new FamLinux;
    f->in=inotify_init1(IN_NONBLOCK|IN_CLOEXEC);
    f->notify=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
    fc->fd=epoll_create1(EPOLL_CLOEXEC);fc->client=f;
    if(f->in<0 || f->notify<0 || fc->fd<0) { FAMClose(fc); return -1; }
    epoll_event e;memset(&e,0,sizeof e);e.events=EPOLLIN;
    e.data.fd=f->in;if(epoll_ctl(fc->fd,EPOLL_CTL_ADD,f->in,&e)<0){FAMClose(fc);return -1;}
    e.data.fd=f->notify;if(epoll_ctl(fc->fd,EPOLL_CTL_ADD,f->notify,&e)<0){FAMClose(fc);return -1;}
    return 0;
}
extern "C" int FAMClose(FAMConnection *fc) {
    FamLinux *f=(FamLinux *)fc->client;
    if(f){if(f->in>=0)close(f->in);if(f->notify>=0)close(f->notify);delete f;}
    if(fc->fd>=0)close(fc->fd);fc->fd=-1;fc->client=0;return 0;
}
static int monitor(FAMConnection *fc,const char *path,FAMRequest *r,bool dir) {
    FamLinux *f=(FamLinux *)fc->client;Watch w;
    w.path=path;w.directory=dir;w.req=r->reqnum;
    std::string parent=path;
    if(!dir){size_t pos=parent.rfind('/');w.name=parent.substr(pos+1);parent=parent.substr(0,pos);if(parent.empty())parent="/";}
    w.wd=inotify_add_watch(f->in,parent.c_str(),IN_CLOSE_WRITE|IN_CREATE|IN_DELETE|IN_MOVED_FROM|IN_MOVED_TO|IN_ATTRIB|IN_DELETE_SELF|IN_MOVE_SELF);
    if(w.wd<0)return -1;f->watches.push_back(w);queue(fc,w,FAMEndExist);return 0;
}
extern "C" int FAMMonitorFile2(FAMConnection *fc,const char *p,FAMRequest *r){return monitor(fc,p,r,false);}
extern "C" int FAMMonitorDirectory2(FAMConnection *fc,const char *p,FAMRequest *r){return monitor(fc,p,r,true);}
extern "C" int FAMPending(FAMConnection *fc) {
    FamLinux *f=(FamLinux *)fc->client;
    union { char data[16384]; inotify_event align; } buffer;
    ssize_t n;
    while((n=read(f->in,buffer.data,sizeof buffer.data))>0){
        for(ssize_t i=0;i<n;){inotify_event *e=(inotify_event *)(buffer.data+i);
            for(size_t k=0;k<f->watches.size();k++){
                const Watch& w=f->watches[k];
                if(e->mask&IN_Q_OVERFLOW || (e->wd==w.wd && (w.directory || !e->len || w.name==e->name))) queue(fc,w,FAMChanged);
            }
            i+=sizeof *e+e->len;
        }
    }
    return f->pending.size();
}
extern "C" int FAMNextEvent(FAMConnection *fc,FAMEvent *e) {
    FamLinux *f=(FamLinux *)fc->client;
    if(!FAMPending(fc)){errno=EAGAIN;return -1;}
    *e=f->pending.front();f->pending.pop_front();
    if(f->pending.empty()){uint64_t v;while(read(f->notify,&v,sizeof v)>0){}}
    return 1;
}
/* Paths.  By default mediad uses the same absolute paths as on IRIX
 * (/etc/config/mediad.config, /etc/fsd.tab, /<device name> mount points,
 * /tmp/.mediad_socket).  MEDIAD_STATE, if set, redirects them into a private
 * directory (used for testing without touching the system). */
const char *linux_state_path(const char *leaf) {
    static std::map<std::string,std::string> paths;
    std::string key=leaf;
    if(paths.find(key)==paths.end()){
        const char *s=getenv("MEDIAD_STATE");
        if(s && *s=='/'){
            paths[key]=std::string(s)+"/"+leaf;
        } else {
            static const struct { const char *leaf, *path; } irix[] = {
                { "mediad.config", "/etc/config/mediad.config" },
                { "fsd.tab",       "/etc/fsd.tab" },
                { "fsd.auto",      "/etc/fsd.auto" },
                { "fsdXXXXXX",     "/etc/fsdXXXXXX" },
                { "mediad.sock",   "/tmp/.mediad_socket" },
                { "media",         "" },          /* mount points are /<name> */
            };
            std::string path;
            for (size_t i = 0; i < sizeof irix / sizeof irix[0]; ++i)
                if (key == irix[i].leaf) path = irix[i].path;
            if (path.empty() && key != "media") path = std::string("/etc/") + leaf;
            paths[key]=path;
        }
    }
    return paths[key].c_str();
}
/* Mount/eject/export actions are on by default for root (as on IRIX);
 * MEDIAD_ENABLE_ACTIONS=0/1 overrides. */
bool linux_actions_enabled(){
    const char *e=getenv("MEDIAD_ENABLE_ACTIONS");
    if (e) return !strcmp(e,"1");
    return getuid()==0;
}
const char *linux_module_dir(){const char *p=getenv("MEDIAD_MODULE_DIR");return p?p:"/usr/lib/devicelib";}

/* ---- udisks mount helpers ------------------------------------------------
 * Mount removable media as the invoking user through udisks2 (no root), then
 * make mediad's own mount directory (dir()) a symlink to wherever udisks
 * mounted it, so the path mediad reports to clients stays stable. */
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <irixdev.h>

static int run_capture(const char *const argv[], char *out, size_t outsz)
{
    int fds[2];
    pid_t pid;
    size_t n = 0;
    int status = -1;
    if (pipe(fds) != 0) return -1;
    pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        dup2(fds[1], 1); dup2(fds[1], 2);
        close(fds[0]); close(fds[1]);
        execv(argv[0], (char *const *) argv);
        _exit(127);
    }
    close(fds[1]);
    for (;;) {
        ssize_t r = read(fds[0], out + n, outsz - 1 - n);
        if (r <= 0) break;
        n += (size_t) r;
        if (n >= outsz - 1) break;
    }
    out[n] = 0;
    close(fds[0]);
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int block_for(const char *irix_dev, char *blk, size_t n)
{
    if (irixdev_resolve(irix_dev, blk, n) != 0) return -1;
    if (strncmp(blk, "/dev/", 5) != 0) return -1;
    for (const char *c = blk + 5; *c; ++c)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
              (*c >= '0' && *c <= '9') || *c == '_' || *c == '-' || *c == '/'))
            return -1;
    return 0;
}

int linux_media_mount(const char *irix_dev, const char *link)
{
    char blk[256], out[1024];
    const char *argv[] = { "/usr/bin/udisksctl", "mount", "-b", blk, "-o", "ro", NULL };
    if (block_for(irix_dev, blk, sizeof blk) != 0) return -1;
    int rc = run_capture(argv, out, sizeof out);
    char *at = strstr(out, " at ");
    if (!at) at = strstr(out, "already mounted at");
    char path[512] = "";
    if (at) {
        char *p = strstr(at, "at ") + 3;
        if (*p == '`' || *p == '\'') ++p;
        size_t k = 0;
        while (*p && *p != '\n' && *p != '\'' && k < sizeof path - 1) path[k++] = *p++;
        while (k && (path[k-1] == '.' || path[k-1] == ' ')) --k;
        path[k] = 0;
    }
    if (path[0] != '/' || (rc != 0 && !strstr(out, "already mounted"))) {
        fprintf(stderr, "mediad: udisksctl mount failed: %s", out);
        return -1;
    }
    // dir() may be a leftover empty directory or a stale symlink.
    unlink(link);
    rmdir(link);
    if (symlink(path, link) != 0) return -1;
    return 0;
}

int linux_media_unmount(const char *irix_dev, const char *link)
{
    char blk[256], out[1024];
    const char *argv[] = { "/usr/bin/udisksctl", "unmount", "-b", blk, NULL };
    if (block_for(irix_dev, blk, sizeof blk) != 0) return -1;
    int rc = run_capture(argv, out, sizeof out);
    if (rc != 0 && !strstr(out, "not mounted")) {
        fprintf(stderr, "mediad: udisksctl unmount failed: %s", out);
        return -1;
    }
    unlink(link);
    return 0;
}

int linux_media_block(const char *irix_dev, char *blk, size_t n)
{
    return block_for(irix_dev, blk, n);
}
/* Mount ownership: whoever owns the private state directory. */
static struct stat state_stat(void)
{
    struct stat st;
    memset(&st, 0, sizeof st);
    const char *d = getenv("MEDIAD_STATE");
    if (d) stat(d, &st);
    return st;
}
unsigned linux_media_owner_uid(void) { return (unsigned) state_stat().st_uid; }
unsigned linux_media_owner_gid(void) { return (unsigned) state_stat().st_gid; }
