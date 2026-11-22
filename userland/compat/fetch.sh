#!/bin/sh
# Copy the IRIX headers the host lacks into compat/irix-include (not committed).  Paths literal.
set -e
S=/home/nekron/Projects/irix-657m-src
D=/home/nekron/Projects/irix-work/userland/compat/irix-include
mkdir -p $D
mkdir -p $D/sys
cp $S/eoe/include/tzfile.h $D/
cp $S/irix/kern/sys/sysmp.h $S/irix/kern/sys/syssgi.h $D/sys/
# IRIX versions that must win over the host's: they carry IRIX's own extensions (MM_REASON, DEF_NLSPATH)
cp $S/eoe/include/fmtmsg.h $S/eoe/include/nl_types.h $D/
