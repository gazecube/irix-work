// SPDX-License-Identifier: GPL-2.0
/*
 * IRIX System V pseudo-terminal controller/slave compatibility driver.
 *
 * This is a source-driven port of the state machine in SGI IRIX
 * kern/io/spty.c (Revision 3.62), adapted to Linux kernel primitives.
 * It deliberately keeps IRIX PTC/PTS state and ioctl semantics visible
 * instead of translating /dev/ptc to Linux ptmx in userspace.
 *
 * Phase 1 establishes the real kernel device, clone allocation, persistent
 * per-pair state, and the IRIX clone-control ioctls. Data-path and STREAMS
 * message semantics are layered onto this substrate in subsequent passes.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/idr.h>
#include <linux/poll.h>

#include "irix_spty.h"

static DEFINE_IDA(irix_spty_ids);
static DEFINE_MUTEX(irix_spty_table_lock);
static struct irix_spty *irix_spty_table[IRIX_SPTY_MAXPTY];
static unsigned int maxpty = IRIX_SPTY_MAXPTY;
module_param(maxpty, uint, 0444);
MODULE_PARM_DESC(maxpty, "maximum IRIX pseudo terminals (1..1000)");

static struct irix_spty *irix_spty_alloc(void)
{
	struct irix_spty *pt;
	int id;

	id = ida_alloc_range(&irix_spty_ids, 0, maxpty - 1, GFP_KERNEL);
	if (id < 0)
		return ERR_PTR(id);

	pt = kzalloc(sizeof(*pt), GFP_KERNEL);
	if (!pt) {
		ida_free(&irix_spty_ids, id);
		return ERR_PTR(-ENOMEM);
	}

	mutex_init(&pt->lock);
	init_waitqueue_head(&pt->master_wait);
	init_waitqueue_head(&pt->slave_wait);
	pt->index = id;
	pt->pts_state = IRIX_PT_UNBORN | IRIX_PTS_LOCKED;
	pt->ptc_state = IRIX_PT_UNBORN;
	pt->pts_auto_ld = 1;
	pt->termios.c_cflag = CS8 | CREAD | CLOCAL;

	mutex_lock(&irix_spty_table_lock);
	irix_spty_table[id] = pt;
	mutex_unlock(&irix_spty_table_lock);
	return pt;
}

static void irix_spty_release_pair(struct irix_spty *pt)
{
	unsigned int id = pt->index;

	mutex_lock(&irix_spty_table_lock);
	if (irix_spty_table[id] == pt)
		irix_spty_table[id] = NULL;
	mutex_unlock(&irix_spty_table_lock);
	ida_free(&irix_spty_ids, id);
	kfree(pt);
}

static int irix_ptc_open(struct inode *inode, struct file *file)
{
	struct irix_spty *pt;

	pt = irix_spty_alloc();
	if (IS_ERR(pt))
		return PTR_ERR(pt);

	mutex_lock(&pt->lock);
	pt->master_open = 1;
	pt->ptc_state &= ~(IRIX_PT_UNBORN | IRIX_PT_DEAD);
	mutex_unlock(&pt->lock);
	file->private_data = pt;

	pr_debug("irix_spty: ptc clone open -> pty %u\n", pt->index);
	return 0;
}

static int irix_ptc_release(struct inode *inode, struct file *file)
{
	struct irix_spty *pt = file->private_data;
	bool free_pair;

	if (!pt)
		return 0;

	mutex_lock(&pt->lock);
	pt->master_open = 0;
	pt->ptc_state |= IRIX_PT_DEAD;
	pt->ptc_state &= ~IRIX_PTC_QUEUED;
	memset(&pt->winsize, 0, sizeof(pt->winsize));
	free_pair = !pt->slave_open;
	mutex_unlock(&pt->lock);
	wake_up_all(&pt->slave_wait);

	if (free_pair)
		irix_spty_release_pair(pt);
	return 0;
}

static long irix_ptc_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct irix_spty *pt = file->private_data;
	int value;

	if (!pt)
		return -ENODEV;

	mutex_lock(&pt->lock);
	switch (cmd) {
	case IRIX_ISPTM:
		/* Original spty.c ACKs this with no payload. */
		mutex_unlock(&pt->lock);
		return 0;

	case IRIX_UNLKPT:
		pt->pts_state &= ~IRIX_PTS_LOCKED;
		mutex_unlock(&pt->lock);
		wake_up_all(&pt->slave_wait);
		return 0;

	case IRIX_SVR4SOPEN:
		pt->pts_auto_ld = 0;
		mutex_unlock(&pt->lock);
		return 0;

	case TIOCGWINSZ:
		if (copy_to_user((void __user *)arg, &pt->winsize,
				 sizeof(pt->winsize))) {
			mutex_unlock(&pt->lock);
			return -EFAULT;
		}
		mutex_unlock(&pt->lock);
		return 0;

	case TIOCSWINSZ:
		if (copy_from_user(&pt->winsize, (void __user *)arg,
				   sizeof(pt->winsize))) {
			mutex_unlock(&pt->lock);
			return -EFAULT;
		}
		mutex_unlock(&pt->lock);
		wake_up_all(&pt->slave_wait);
		return 0;

	case TIOCPKT:
		if (get_user(value, (int __user *)arg)) {
			mutex_unlock(&pt->lock);
			return -EFAULT;
		}
		if (value) {
			pt->ptc_state |= IRIX_PTC_PKT;
			pt->pt_pkt = TIOCPKT_DATA;
		} else {
			pt->ptc_state &= ~IRIX_PTC_PKT;
		}
		pt->ptc_state |= IRIX_PTC_PKT_SET;
		mutex_unlock(&pt->lock);
		return 0;

	default:
		mutex_unlock(&pt->lock);
		return -ENOTTY;
	}
}

static __poll_t irix_ptc_poll(struct file *file, poll_table *wait)
{
	struct irix_spty *pt = file->private_data;
	__poll_t mask = 0;

	if (!pt)
		return EPOLLERR;

	poll_wait(file, &pt->master_wait, wait);
	mutex_lock(&pt->lock);
	if (pt->ptc_state & IRIX_PT_DEAD)
		mask |= EPOLLHUP;
	if (pt->pt_pkt)
		mask |= EPOLLIN | EPOLLRDNORM;
	mutex_unlock(&pt->lock);
	return mask;
}

static const struct file_operations irix_ptc_fops = {
	.owner          = THIS_MODULE,
	.open           = irix_ptc_open,
	.release        = irix_ptc_release,
	.unlocked_ioctl = irix_ptc_ioctl,
	.compat_ioctl   = irix_ptc_ioctl,
	.poll           = irix_ptc_poll,
	.llseek         = no_llseek,
};

static struct miscdevice irix_ptc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ptc",
	.fops = &irix_ptc_fops,
	.mode = 0666,
};

static int __init irix_spty_init(void)
{
	int rc;

	if (!maxpty || maxpty > IRIX_SPTY_MAXPTY)
		return -EINVAL;

	rc = misc_register(&irix_ptc_device);
	if (rc)
		return rc;

	pr_info("irix_spty: IRIX PTC/PTS substrate loaded, maxpty=%u, /dev/ptc ready\n",
		maxpty);
	return 0;
}

static void __exit irix_spty_exit(void)
{
	unsigned int i;

	misc_deregister(&irix_ptc_device);
	mutex_lock(&irix_spty_table_lock);
	for (i = 0; i < IRIX_SPTY_MAXPTY; ++i) {
		struct irix_spty *pt = irix_spty_table[i];
		if (!pt)
			continue;
		irix_spty_table[i] = NULL;
		ida_free(&irix_spty_ids, i);
		kfree(pt);
	}
	mutex_unlock(&irix_spty_table_lock);
	ida_destroy(&irix_spty_ids);
	pr_info("irix_spty: unloaded\n");
}

module_init(irix_spty_init);
module_exit(irix_spty_exit);

MODULE_DESCRIPTION("IRIX System V PTC/PTS source port substrate");
MODULE_AUTHOR("irix-re");
MODULE_LICENSE("GPL");
