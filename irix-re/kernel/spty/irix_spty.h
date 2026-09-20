/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux substrate for the IRIX System V PTC/PTS driver.
 *
 * Behavioral source: IRIX kern/io/spty.c, revision 3.62.
 */
#ifndef IRIX_SPTY_H
#define IRIX_SPTY_H

#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/termios.h>

#define IRIX_SPTY_MAXPTY 1000
#define IRIX_SPTY_PER_MAJOR 200

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

#define IRIX_ISPTM           (('P' << 8) | 1)
#define IRIX_UNLKPT          (('P' << 8) | 2)
#define IRIX_SVR4SOPEN       (('P' << 8) | 100)

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
};

#endif
