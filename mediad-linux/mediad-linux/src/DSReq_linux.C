#include "DSReq.H"
#include <errno.h>
DSReq::DSReq(const SCSIAddress& a): _addr(a) {}
DSReq::~DSReq() {}
dsreq *DSReq::dsptr() { errno = ENOTSUP; return 0; }
int DSReq::g0cmd(uc,uc,uc,uc,uc,uc) { return ENOTSUP; }
int DSReq::g1cmd(uc,uc,uc,uc,uc,uc,uc,uc,uc,uc) { return ENOTSUP; }
void DSReq::close_all() {}
