from pathlib import Path
p=Path('outputs/mediad-linux')
def edit(name,a,b):
 f=p/name;s=f.read_text();assert a in s,(name,a);f.write_text(s.replace(a,b))
s=(p/'src/Config.C').read_text();start=s.index('enum Token {');end=s.index('};',start)+2;enum=s[start:end];(p/'src/Token.H').write_text('#pragma once\n'+enum+'\n');(p/'src/Config.C').write_text(s[:start]+s[end:])
edit('src/Config.H','enum  FormatIndex;\nenum  Token;','#include "FormatIndex.H"\n#include "Token.H"')
edit('src/Device.C','for (Device *p = first(); p; p = p->next())','Device *p;\n    for (p = first(); p; p = p->next())')
edit('src/DeviceMonitor.C','for (DeviceMonitor *p = first(); p; p = p->next())','DeviceMonitor *p;\n    for (p = first(); p; p = p->next())')
edit('src/Enumerable.C','for (unsigned int i = 0; p != this; i++, p = p->next())','unsigned int i;\n    for (i = 0; p != this; i++, p = p->next())')
edit('src/Enumerable.C','assert(_set._last = this);','assert(_set._last == this);')
edit('src/Fsd.C','for (int i = 0; mnt = getmntent(f); i++)','int i;\n    for (i = 0; mnt = getmntent(f); i++)')
edit('src/Volume.C','dp->d_namlen','strlen(dp->d_name)')
for name in ['volume','device','partition']:
 edit('src/MonitorClient.C',f', MonitorClient::xdr_{name})',f', &MonitorClient::xdr_{name})')
for name in ['volume','device','partition']:
 edit('src/xdr_mc.c',f'      xdr_mc_{name}',f'      (xdrproc_t) xdr_mc_{name}')
edit('src/xdr_mc.c','xdr_uint64(','xdr_uint64_t(')
for name in ['CompatListener.C','RPCListener.C']:
 edit('src/'+name,'int addrlen = sizeof addr;','socklen_t addrlen = sizeof addr;')
edit('src/main.C','_daemonize(0, -1, -1, -1);','daemon(0, 0);')
edit('include/linux_compat.h','typedef unsigned int uint;','typedef unsigned int uint;\ntypedef void (*SIG_PF)(int);')
edit('include/sys/invent.h', (p/'include/sys/invent.h').read_text(), '#pragma once\n#include <invent_compat.h>\n')
f=Path('outputs/irix-device-ecosystem/include/invent_compat.h');f.write_text(f.read_text()+'\n#define INV_SCSIQIC 1\n')
edit('Makefile','-Iinclude -Isrc','-Iinclude -Isrc -I../irix-device-ecosystem/include')
edit('Makefile','-ltirpc -ldl -o','../irix-device-ecosystem/build/libirixdev.a -ltirpc -ldl -o')
# Replace IRIX hardware graph path resolution with the shared namespace.
f=p/'src/VolumeAddress.C';s=f.read_text();s=s.replace('#include <devconvert.h>','#include <irixdev.h>');s=s.replace('#include <diskinvent.h>','').replace('#include <sys/conf.h>','')
s=s[:s.index('static bool match_scsi_dev')]+'''VolumeAddress::VolumeAddress(const char *path, FormatIndex fmt, int partno)
: _refcount(0), _rep(NULL)
{
    irixdev_device d;
    int slice;
    if (irixdev_lookup(path, &d, &slice) < 0) return;
    if (partno == UnknownPartition) partno = slice < 0 ? WholeDisk : slice;
    _rep = new PartitionAddress(DeviceAddress(d.controller, d.target, d.lun), fmt, partno);
}
''';f.write_text(s)
