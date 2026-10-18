/* Linux monitor-service listener.  Same protocol as the IRIX original: a TCP
 * rendezvous socket registered with the portmapper as program 391017 v1, so
 * the unmodified IRIX libmediaclient finds it by PMAP_GETPORT.  Unlike IRIX
 * we bind loopback only (and a non-reserved port): this exposes the machine's
 * drives and must not be reachable from the network. */
#include "RPCListener.H"
#include "MonitorClient.H"
#include "Log.H"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <unistd.h>
#include <string.h>

RPCListener::RPCListener(bool all,bool inetd,u_long prog,u_long vers)
: _all_hosts(all),_started_by_inetd(inetd),_program(prog),_version(vers),_rendezvous_handler(accept_client,this){
    int sock=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);
    if(sock<0){Log::perror("can't create TCP rendezvous socket");exit(1);}
    int one=1;setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    sockaddr_in a;memset(&a,0,sizeof a);
    a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=0;
    if(bind(sock,(sockaddr *)&a,sizeof a)<0){Log::perror("can't bind rendezvous socket");exit(1);}
    socklen_t len=sizeof a;
    if(getsockname(sock,(sockaddr *)&a,&len)<0 || listen(sock,8)<0){Log::perror("can't listen for rendezvous");exit(1);}
    unsigned short port=ntohs(a.sin_port);
    if(!pmap_set(_program,_version,IPPROTO_TCP,port)){
        (void)pmap_unset(_program,_version);
        if(!pmap_set(_program,_version,IPPROTO_TCP,port)){
            Log::error("can't register with portmapper (is rpcbind running?)");
            close(sock);return;
        }
    }
    _rendezvous_handler.fd(sock);_rendezvous_handler.activate();
    Log::debug("listening for monitor clients on 127.0.0.1:%u (program %lu)",port,_program);
}
RPCListener::~RPCListener(){
    int fd=_rendezvous_handler.fd();
    if(fd>=0){pmap_unset(_program,_version);close(fd);}
}
void RPCListener::accept_client(int fd,void *){
    int client=accept4(fd,0,0,SOCK_CLOEXEC);
    if(client<0)Log::perror("failed to accept new client");
    else (void)new MonitorClient(client,false);
}
