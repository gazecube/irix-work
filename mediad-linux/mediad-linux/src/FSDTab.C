#include "FSDTab.H"

#include <errno.h>
#include <mntent.h>
#include <string.h>
#include <unistd.h>

#include "CallBack.H"
#include "Log.H"
#include "Volume.H"

const char *FSDTab::fsdtab_path = linux_state_path("fsd.tab");

FSDTab::FSDTab()
{
    update();

    CallBack::add(event_proc, this);
}

FSDTab::~FSDTab()
{
    CallBack::remove(event_proc, this);

    (void) unlink(fsdtab_path);
}

void
FSDTab::event_proc(const Event&, void *closure)
{
    FSDTab *p = (FSDTab *) closure;

    p->update();
}

void
FSDTab::update()
{
    char tmpname[PATH_MAX];
    snprintf(tmpname,sizeof tmpname,"%s.XXXXXX",fsdtab_path);
    int fd=mkstemp(tmpname);
    if(fd<0){Log::perror("fsd.tab temporary file");return;}
    FILE *f=fdopen(fd,"w");
    if(!f){close(fd);unlink(tmpname);return;}
    bool failed=false;
    for(Volume *v=Volume::first();v;v=v->next()){
        mntent m={(char *)v->fsname(),(char *)v->dir(),(char *)v->type(),(char *)v->options(),0,0};
        if(addmntent(f,&m)) {failed=true;break;}
    }
    if(fflush(f) || fsync(fd))failed=true;
    if(fclose(f))failed=true;
    if(!failed && rename(tmpname,fsdtab_path)==0)return;
    unlink(tmpname);Log::perror("updating fsd.tab");
}
