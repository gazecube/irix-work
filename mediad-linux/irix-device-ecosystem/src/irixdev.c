#include "irixdev.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>

static FILE *manifest(const char *name) {
    char path[PATH_MAX];
    const char *state=getenv("IRIXDEV_STATE");
    if (!state || !*state) state="/var/lib/irix-devices";
    if (snprintf(path,sizeof path,"%s/%s",state,name)>=(int)sizeof path) { errno=ENAMETOOLONG; return NULL; }
    return fopen(path,"r");
}
static int copystr(char *out,size_t cap,const char *s) {
    if (!out || strlen(s)>=cap) { errno=ENAMETOOLONG; return -1; }
    memcpy(out,s,strlen(s)+1); return 0;
}
static int parse(char *line,struct irixdev_device *d) {
    char *f[8], *p=line;
    memset(d,0,sizeof *d);
    line[strcspn(line,"\n")]=0;
    for (int i=0;i<8;i++) { f[i]=strsep(&p,"\t"); if (!f[i]) { errno=EINVAL; return -1; } }
    char *end;
    unsigned *numbers[]={&d->controller,&d->target,&d->lun};
    for (int i=0;i<3;i++) {
        unsigned long v=strtoul(f[i],&end,10);
        if (!*f[i] || *end || v>INT_MAX || (i==1 && v>15) || (i==2 && v>7)) { errno=EINVAL; return -1; }
        *numbers[i]=(unsigned)v;
    }
    if (strcmp(f[3],"cdrom") && strcmp(f[3],"disk")) { errno=EINVAL; return -1; }
    d->optical=!strcmp(f[3],"cdrom");
    return copystr(d->block,sizeof d->block,f[4]) || copystr(d->scsi,sizeof d->scsi,strcmp(f[5],"-")?f[5]:"") || copystr(d->raw_name,sizeof d->raw_name,f[6]) || copystr(d->identity,sizeof d->identity,f[7]) ? -1:0;
}
int irixdev_list(struct irixdev_device *out,size_t cap) {
    FILE *f=manifest("devices.tsv"); if (!f) return -1;
    char *line=NULL; size_t size=0; int n=0,rc=0; struct irixdev_device d;
    while (getline(&line,&size,f)>0) {
        if (line[0]=='\n') continue;
        if (parse(line,&d)<0) { rc=-1; break; }
        if (out && (size_t)n<cap) out[n]=d;
        n++;
    }
    if (ferror(f)) rc=-1;
    free(line); fclose(f);
    if (rc<0) return -1;
    if (out && (size_t)n>cap) { errno=ENOSPC; return -1; }
    return n;
}
static int same(const char *a,const char *b) {
    struct stat x,y;
    if (!strcmp(a,b)) return 1;
    return !stat(a,&x) && !stat(b,&y) &&
        (S_ISBLK(x.st_mode)||S_ISCHR(x.st_mode)) &&
        ((x.st_mode&S_IFMT)==(y.st_mode&S_IFMT)) && x.st_rdev==y.st_rdev;
}
static const char *guestpath(const char *p) {
    const char *root=getenv("IRIXDEV_ROOT");
    if (root && *root && !strncmp(p,root,strlen(root)) && p[strlen(root)]=='/') return p+strlen(root);
    return p;
}
int irixdev_resolve(const char *path,char *out,size_t cap) {
    path=guestpath(path);
    FILE *f=manifest("aliases.tsv"); if (!f) return -1;
    char *line=NULL; size_t size=0; int rc=-1;
    errno=ENOENT;
    while (getline(&line,&size,f)>0) {
        line[strcspn(line,"\n")]=0;
        char *tab=strchr(line,'\t'); if (!tab) continue; *tab++=0;
        if (!strcmp(path,line)) { rc=copystr(out,cap,tab); break; }
    }
    free(line); fclose(f); return rc;
}
int irixdev_lookup(const char *path,struct irixdev_device *out,int *slice) {
    int n=irixdev_list(NULL,0); if (n<0) return -1;
    struct irixdev_device *ds=calloc(n?n:1,sizeof *ds); if (!ds) return -1;
    if (irixdev_list(ds,n)<0) { free(ds); return -1; }
    const char *gp=guestpath(path); char target[PATH_MAX];
    if (irixdev_resolve(gp,target,sizeof target)<0) { if (copystr(target,sizeof target,path)<0) {free(ds);return -1;} }
    int rc=-1;
    for (int i=0;i<n;i++) {
        if (same(target,ds[i].block) || (*ds[i].scsi && same(target,ds[i].scsi))) {
            if (out) *out=ds[i]; if (slice) *slice=-1; rc=0; break;
        }
        /* Explicit slice aliases must encode this drive's controller/target. */
        unsigned c,t; int part,used=0;
        if ((sscanf(gp,"/dev/dsk/dks%ud%us%d%n",&c,&t,&part,&used)==3 ||
             sscanf(gp,"/dev/rdsk/dks%ud%us%d%n",&c,&t,&part,&used)==3) &&
            !gp[used] && c==ds[i].controller && t==ds[i].target && strcmp(target,path) && part>=0 && part<16) {
            if (out) *out=ds[i]; if (slice) *slice=part; rc=0; break;
        }
    }
    free(ds); if (rc<0) errno=ENODEV; return rc;
}
int irixdev_address(unsigned c,unsigned t,unsigned l,struct irixdev_device *out) {
    int n=irixdev_list(NULL,0); if(n<0)return -1;
    struct irixdev_device *ds=calloc(n?n:1,sizeof *ds); if(!ds)return -1;
    if(irixdev_list(ds,n)<0) {free(ds);return -1;}
    int rc=-1;for(int i=0;i<n;i++)if(ds[i].controller==c && ds[i].target==t && ds[i].lun==l){*out=ds[i];rc=0;break;}
    free(ds);if(rc<0)errno=ENODEV;return rc;
}
static inventory_t *inventory; static int ninventory,index_inventory;
int setinvent(void) {
    endinvent(); int n=irixdev_list(NULL,0); if(n<0)return -1;
    struct irixdev_device *ds=calloc(n?n:1,sizeof *ds);
    inventory=calloc(n?n:1,sizeof *inventory);
    if(!ds || !inventory){free(ds);endinvent();return -1;}
    if(irixdev_list(ds,n)<0){free(ds);endinvent();return -1;}
    ninventory=n;
    for(int i=0;i<n;i++){
        inventory[i].inv_class=ds[i].optical?INV_SCSI:INV_DISK;
        inventory[i].inv_type=ds[i].optical?INV_CDROM:2;
        inventory[i].inv_controller=ds[i].controller;inventory[i].inv_unit=ds[i].target;
        inventory[i].inv_state=ds[i].lun<<8;
        inventory[i].inv_next=i+1<n?&inventory[i+1]:NULL;
    }
    free(ds);return 0;
}
inventory_t *getinvent(void){return index_inventory<ninventory?&inventory[index_inventory++]:NULL;}
void endinvent(void){free(inventory);inventory=NULL;ninventory=index_inventory=0;}
char *filename_to_scsiname(const char *path,char *out,int *len){
    struct irixdev_device d;char name[128];
    if(irixdev_lookup(path,&d,NULL)<0)return NULL;
    if(!*d.scsi){errno=ENOTSUP;return NULL;}
    snprintf(name,sizeof name,"/dev/scsi/sc%ud%ul%u",d.controller,d.target,d.lun);
    size_t need=strlen(name)+1; if(*len<(int)need){*len=(int)need;errno=ENOSPC;return NULL;}
    memcpy(out,name,need);*len=(int)need;return out;
}
char *filename_to_diskname(const char *path,int raw,int part,char *out,int *len){
    struct irixdev_device d;char name[128],suffix[32];
    if(irixdev_lookup(path,&d,NULL)<0)return NULL;
    if(part<0 || part==10)strcpy(suffix,"vol");else if(part==8)strcpy(suffix,"vh");else snprintf(suffix,sizeof suffix,"s%d",part);
    snprintf(name,sizeof name,"/dev/%s/dks%ud%u%s",raw?"rdsk":"dsk",d.controller,d.target,suffix);
    char target[PATH_MAX];if(irixdev_resolve(name,target,sizeof target)<0)return NULL;
    size_t need=strlen(name)+1;if(*len<(int)need){*len=need;errno=ENOSPC;return NULL;}memcpy(out,name,need);*len=need;return out;
}
int path_to_partnum(char *path){struct irixdev_device d;int slice;if(irixdev_lookup(path,&d,&slice)<0)return -1;return slice;}
