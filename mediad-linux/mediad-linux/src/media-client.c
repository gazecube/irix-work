/* Native C client exercising the original libmediaclient public API. */
#include <mediaclient.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int received;
static void string(const char *s){
    putchar('"');for(const unsigned char *p=(const unsigned char *)s;*p;p++){
        if(*p=='"'||*p=='\\')printf("\\%c",*p);
        else if(*p<32)printf("\\u%04x",*p);else putchar(*p);
    }putchar('"');
}
static void event(mc_port_t port,mc_closure_t closure,const mc_event_t *ev){
    (void)closure;
    const mc_system_t *sys=mc_system_state(port);
    printf("{\"event\":%d,\"devices\":[",ev->mce_type);
    for(unsigned i=0;i<sys->mcs_n_devices;i++){
        const mc_device_t *d=&sys->mcs_devices[i];if(i)putchar(',');
        printf("{\"name\":");string(d->mcd_name);printf(",\"path\":");string(d->mcd_devname);
        printf(",\"controller\":%u,\"target\":%u,\"media_present\":%d}",d->mcd_invent.inv_controller,d->mcd_invent.inv_unit,d->mcd_media_present);
    }
    printf("],\"volumes\":[");
    for(unsigned i=0;i<sys->mcs_n_volumes;i++){
        const mc_volume_t *v=&sys->mcs_volumes[i];if(i)putchar(',');
        printf("{\"label\":");string(v->mcv_label);printf(",\"filesystem\":");string(v->mcv_type);
        printf(",\"path\":");string(v->mcv_fsname);printf(",\"directory\":");string(v->mcv_dir);
        printf(",\"mounted\":%d}",v->mcv_mounted);
    }
    printf("],\"partitions\":%u}\n",sys->mcs_n_parts);fflush(stdout);received++;
}
int main(int argc,char **argv){
    int watch=argc==2 && !strcmp(argv[1],"--watch");
    if(argc>2 || (argc==2 && !watch)){fprintf(stderr,"usage: media-client [--watch]\n");return 2;}
    mc_port_t p=mc_create_port(htonl(INADDR_LOOPBACK),event,NULL);
    if(!p){perror("mc_create_port");return 1;}
    int fd=mc_port_connect(p);
    if(fd<0){fprintf(stderr,"cannot connect to mediad\n");mc_destroy_port(p);return 1;}
    do{
        fd_set r;FD_ZERO(&r);FD_SET(fd,&r);struct timeval tv={10,0};
        int n=select(fd+1,&r,NULL,NULL,watch?NULL:&tv);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){fprintf(stderr,"no media event received\n");mc_destroy_port(p);return 1;}
        const mc_what_next_t *next=mc_execute(p);
        if(next->mcwn_what!=MC_INPUT){fprintf(stderr,"media connection lost (%d)\n",next->mcwn_failcode);mc_destroy_port(p);return 1;}
    }while(watch || !received);
    mc_destroy_port(p);return 0;
}
