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
const char *linux_state_path(const char *leaf) {
    static std::map<std::string,std::string> paths;
    std::string key=leaf;
    if(paths.find(key)==paths.end()){
        const char *s=getenv("MEDIAD_STATE");
        if(!s || *s!='/'){fprintf(stderr,"MEDIAD_STATE must name an absolute private state directory\n");exit(1);}
        paths[key]=std::string(s)+"/"+leaf;
    }
    return paths[key].c_str();
}
bool linux_actions_enabled(){const char *e=getenv("MEDIAD_ENABLE_ACTIONS");return e && !strcmp(e,"1");}
const char *linux_module_dir(){const char *p=getenv("MEDIAD_MODULE_DIR");return p?p:"/usr/lib/irix/devicelib";}
