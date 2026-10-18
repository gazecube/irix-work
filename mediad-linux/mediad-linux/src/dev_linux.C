/* Linux optical backend; names/inventory come from the shared IRIX namespace. */
#include "Device.H"
#include "DeviceInfo.H"
#include "PartitionAddress.H"
#include "SCSIAddress.H"
#include "Log.H"
#include <irixdev.h>
#include <linux/cdrom.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

class LinuxOptical : public Device {
    irixdev_device _dev;
    int _last_error;
    bool image() const { struct stat s; return getenv("MEDIAD_IMAGE_BACKEND") && !strcmp(getenv("MEDIAD_IMAGE_BACKEND"),"1") && !stat(_dev.block,&s) && S_ISREG(s.st_mode); }
    int open_drive() const { return open(_dev.block,O_RDONLY|O_NONBLOCK|O_CLOEXEC); }
    int command(unsigned long cmd,int arg) {
        if(!linux_actions_enabled()){errno=EPERM;return -1;}
        int fd=open_drive();if(fd<0)return -1;
        int rc=ioctl(fd,cmd,arg), saved=errno;close(fd);errno=saved;return rc;
    }
public:
    LinuxOptical(const DeviceInfo& info,const irixdev_device& dev):Device(info),_dev(dev),_last_error(0){}
    const char *short_name() const {return "CDROM";}
    const char *full_name() const {return "Linux optical drive";}
    const char *ftr_name() const {return "cdrom";}
    const char *dev_name(FormatIndex,int) const {return _dev.raw_name;}
    int features(){return FILESYS_RAW|FILESYS_ISO9660|FEATURE_MOUNTABLE|FEATURE_RDONLY|FEATURE_SW_EJECT;}
    int is_media_present(){
        if(image()){struct stat s;return !stat(_dev.block,&s) && s.st_size>=32768;}
        int fd=open_drive();if(fd<0){
            if(errno!=_last_error){_last_error=errno;Log::perror("cannot probe %s",_dev.block);}
            return errno==ENOMEDIUM?0:-1;
        }
        _last_error=0;
        int rc=ioctl(fd,CDROM_DRIVE_STATUS,CDSL_CURRENT);close(fd);
        return rc==CDS_DISC_OK?1:(rc==CDS_NO_DISC||rc==CDS_TRAY_OPEN?0:-1);
    }
    int eject(){return command(CDROMEJECT,0);}
    int lock_media(){return command(CDROM_LOCKDOOR,1);}
    int unlock_media(){return command(CDROM_LOCKDOOR,0);}
    int capacity(){
        if(image()){struct stat s;if(stat(_dev.block,&s)<0)return -1;if(s.st_size/2048>INT_MAX){errno=EOVERFLOW;return -1;}return s.st_size/2048;}
        int fd=open_drive();if(fd<0)return -1;unsigned long long bytes=0;
        int rc=ioctl(fd,BLKGETSIZE64,&bytes);close(fd);
        if(rc<0)return -1;
        if(bytes/2048>INT_MAX){errno=EOVERFLOW;return -1;}
        return bytes/2048;
    }
    int sector_size(FormatIndex){return 2048;}
    bool is_write_protected(){return true;}
    int read_data(char *buf,__uint64_t start,unsigned count,unsigned size){
        if(size!=2048 || count>SIZE_MAX/size || start>(unsigned long long)LLONG_MAX/size){errno=EINVAL;return -1;}
        int fd=open_drive();if(fd<0)return -1;
        size_t total=(size_t)count*size,done=0;
        while(done<total){ssize_t n=pread(fd,buf+done,total-done,(off_t)(start*size+done));
            if(n<0 && errno==EINTR)continue;
            if(n<=0){int saved=n<0?errno:EIO;close(fd);errno=saved;return -1;}done+=n;}
        close(fd);return 0;
    }
};
extern "C" Device *instantiate(const DeviceInfo& info){
    const SCSIAddress *a=info.address().as_SCSIAddress();irixdev_device d;
    if(!a || irixdev_address(a->ctlr(),a->id(),a->lun(),&d)<0 || !d.optical)return 0;
    return new LinuxOptical(info,d);
}
