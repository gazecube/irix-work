/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux substrate for the IRIX System V PTC/PTS driver.
 *
 * Behavioral source: IRIX kern/io/spty.c, revision 3.62.
 */
#ifndef IRIX_SPTY_H
#define IRIX_SPTY_H

#include <linux/cdev.h>
#include <linux/kfifo.h>
#include <linux/ioctl.h>
#include <linux/mutex.h>
#include <linux/termios.h>
#include <linux/wait.h>

#define IRIX_SPTY_MAXPTY 1000
#define IRIX_SPTY_PER_MAJOR 200
#define IRIX_SPTY_FIFO_SIZE (64 * 1024)

/* kern/io/spty.c state bits */
#define IRIX_PT_UNBORN       0x01
#define IRIX_PT_DEAD         0x02
#define IRIX_PTS_TXSTOP      0x04
#define IRIX_PTS_LIT         0x08
#define IRIX_PTS_PUSHING     0x10
#define IRIX_PTS_PUSHWAIT    0x20
#define IRIX_PTS_LOCKED      0x40

#define IRIX_PTC_QUEUED      0x04
#define IRIX_PTC_PKT         0x08
#define IRIX_PTC_PKT_SET     0x10

#define IRIX_STR             ('S' << 8)
#define IRIX_I_STR           (IRIX_STR | 010)
#define IRIX_ISPTM           (('P' << 8) | 1)
#define IRIX_UNLKPT          (('P' << 8) | 2)
#define IRIX_SVR4SOPEN       (('P' << 8) | 100)
#define IRIX_TIOC            ('T' << 8)
#define IRIX_TIOCPKT         (IRIX_TIOC | 112)

/*
 * IRIX sys/ioccom.h encoding.  struct winsize is four 16-bit fields,
 * therefore sizeof(struct winsize) is 8 in both the IRIX and Linux ABI.
 */
#define IRIX_IOC_OUT         0x40000000U
#define IRIX_IOC_IN          0x80000000U
#define IRIX_IOC_CMD(f,n,x,y) ((unsigned int)((f) | (((n) & 0xffU) << 16) | \
					       ((unsigned int)(x) << 8) | (y)))
#define IRIX_TIOCGWINSZ      IRIX_IOC_CMD(IRIX_IOC_OUT, 8, 't', 104)
#define IRIX_TIOCSWINSZ      IRIX_IOC_CMD(IRIX_IOC_IN, 8, 't', 103)

/*
 * Host-private query used by the compatibility runtime when translating
 * fstat(2).  This is not an IRIX ioctl and is never exposed to guest code.
 */
#define IRIX_SPTY_HOST_GET_ID _IOR('I', 0xf0, struct irix_spty_host_id)
#define IRIX_SPTY_SIDE_MASTER 0u
#define IRIX_SPTY_SIDE_SLAVE  1u

struct irix_spty_host_id {
	__u32 index;
	__u32 side;
};

struct irix_n32_strioctl {
	__u32 ic_cmd;
	__s32 ic_timout;
	__s32 ic_len;
	__u32 ic_dp;
};

struct irix_spty {
	struct mutex lock;
	wait_queue_head_t master_wait;
	wait_queue_head_t slave_wait;
	unsigned int index;
	unsigned char pt_pkt;
	unsigned char pts_state;
	unsigned char ptc_state;
	unsigned char pts_auto_ld;
	struct winsize winsize;
	struct ktermios termios;
	unsigned int master_open;
	unsigned int slave_open;
	struct kfifo to_master;
	struct kfifo to_slave;
};

#endif
