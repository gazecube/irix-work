// Linux addition: recognise UDF discs (DVD-ROM; many data discs have no
// ISO 9660 bridge).  IRIX's mediad has no UDF support, so this is a separate,
// additive format named "udf" (devices/udf.ftr defines the matching desktop
// type cdrom.udf); the existing IRIX formats and protocol are untouched.
//
// TEMPORARY SPOOF: the rebuilt devices.otr (cdrom.udf) crashes fm, so until
// that is fixed UDF discs are reported to clients as "iso9660" (existing
// cdrom.iso9660 desktop type).  Volume::mount mounts them with -t auto.
// Set MEDIAD_REPORT_UDF=1 to report the real "udf" format.
#include <bstring.h>
#include <mntent.h>
#include <string.h>
#include <stdlib.h>

#include "Device.H"
#include "Log.H"
#include "Partition.H"
#include "PartitionAddress.H"
#include "SimpleVolume.H"

class FormatDSO;

// Decode an ECMA-167 dstring (compression id, bytes, trailing length byte).
static void udf_dstring(const unsigned char *d, int size, char *out, int outsize)
{
    int len = d[size - 1], o = 0;
    if (len > size - 1) len = size - 1;
    if (len >= 1 && d[0] == 8) {
        for (int i = 1; i < len && o < outsize - 1; ++i) out[o++] = (char) d[i];
    } else if (len >= 1 && d[0] == 16) {
        for (int i = 1; i + 1 < len && o < outsize - 1; i += 2)
            out[o++] = d[i + 1] ? (char) d[i + 1] : '?';
    }
    out[o] = 0;
}

extern "C"
void
inspect(FormatDSO *, Device *device)
{
    if (!device->filesys_iso9660())	// optical media only
	return;
    if (device->has_audio_data())
	return;

    char buf[2048];
    int secsize = device->sector_size(FMT_ISO);
    if (secsize != (int) sizeof buf)
	return;

    // Volume recognition sequence: BEA01 .. NSR02/NSR03 .. TEA01 from sector 16.
    bool udf = false;
    for (int s = 16; s < 24 && !udf; ++s) {
	bzero(buf, sizeof buf);
	if (device->read_data(buf, s, 1, secsize) != 0)
	    return;
	if (!strncmp(buf + 1, "NSR02", 5) || !strncmp(buf + 1, "NSR03", 5))
	    udf = true;
	else if (!strncmp(buf + 1, "CD001", 5))
	    return;				// ISO 9660: fmt_iso handles it
	else if (strncmp(buf + 1, "BEA01", 5) && strncmp(buf + 1, "BOOT2", 5) &&
		 strncmp(buf + 1, "TEA01", 5))
	    break;
    }
    if (!udf)
	return;

    // Volume identifier: Primary Volume Descriptor (tag 1) in the main VDS.
    char label[64] = "UDF";
    for (int s = 32; s < 48; ++s) {
	bzero(buf, sizeof buf);
	if (device->read_data(buf, s, 1, secsize) != 0)
	    break;
	if ((unsigned char) buf[0] == 1 && buf[1] == 0) {	// tag id 1
	    char tmp[40];
	    udf_dstring((const unsigned char *) buf + 24, 32, tmp, sizeof tmp);
	    if (tmp[0]) {
		strncpy(label, tmp, sizeof label - 1);
		label[sizeof label - 1] = 0;
	    }
	    break;
	}
    }

    const char *fmtname = getenv("MEDIAD_REPORT_UDF") ? "udf" : "iso9660";
    Log::info("device has UDF filesystem, label \"%s\" (reported as %s).", label, fmtname);
    PartitionAddress paddr(device->address(), FMT_ISO, PartitionAddress::WholeDisk);
    int nsectors = device->capacity();
    Partition *part = Partition::create(paddr, device, secsize, 0, nsectors, fmtname);
    const char *fsname =
	part->device()->dev_name(FMT_ISO, PartitionAddress::WholeDisk);
    mntent mnt = { (char *) fsname, NULL, (char *) fmtname, NULL, 0, 0 };
    (void) SimpleVolume::create(part, mnt, label);
}
