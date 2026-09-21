// SPDX-License-Identifier: GPL-2.0
/*
 * IRIX System V pseudo-terminal controller/slave compatibility driver.
 *
 * Source-driven port of SGI IRIX kern/io/spty.c (Revision 3.62).
 * Linux supplies device, memory, locking and wait-queue primitives; the
 * externally visible state machine follows the IRIX PTC/PTS driver.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/idr.h>
#include <linux/poll.h>
#include <linux/device.h>

#include "irix_spty.h"

static DEFINE_IDA(irix_spty_ids);
static DEFINE_MUTEX(irix_spty_table_lock);
static struct irix_spty *irix_spty_table[IRIX_SPTY_MAXPTY];
static unsigned int maxpty = IRIX_SPTY_MAXPTY;
static dev_t irix_pts_devt;
static struct cdev irix_pts_cdev;
static struct class *irix_pts_class;

module_param(maxpty, uint, 0444);
MODULE_PARM_DESC(maxpty, "maximum IRIX pseudo terminals (1..1000)");

static void irix_slave_name(unsigned int index, char *buf, size_t len)
{
	/* getpty.c's native IRIX form: /dev/tty[q-z][0-99]. */
	snprintf(buf, len, "tty%c%u", "qrstuvwxyz"[index / 100], index % 100);
}

static bool irix_spty_master_readable(struct irix_spty *pt)
{
	return !kfifo_is_empty(&pt->to_master) || pt->pt_pkt ||
	       (pt->pts_state & IRIX_PT_DEAD);
}

static bool irix_spty_slave_readable(struct irix_spty *pt)
{
	return !kfifo_is_empty(&pt->to_slave) ||
	       (pt->ptc_state & IRIX_PT_DEAD);
}

static struct irix_spty *irix_spty_alloc(void)
{
	struct irix_spty *pt;
	int id, rc;

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

	rc = kfifo_alloc(&pt->to_master, IRIX_SPTY_FIFO_SIZE, GFP_KERNEL);
	if (rc)
		goto fail;
	rc = kfifo_alloc(&pt->to_slave, IRIX_SPTY_FIFO_SIZE, GFP_KERNEL);
	if (rc) {
		kfifo_free(&pt->to_master);
		goto fail;
	}

	mutex_lock(&irix_spty_table_lock);
	irix_spty_table[id] = pt;
	mutex_unlock(&irix_spty_table_lock);
	return pt;

fail:
	kfree(pt);
	ida_free(&irix_spty_ids, id);
	return ERR_PTR(rc);
}

static void irix_spty_release_pair(struct irix_spty *pt)
{
	unsigned int id = pt->index;

	mutex_lock(&irix_spty_table_lock);
	if (irix_spty_table[id] == pt)
		irix_spty_table[id] = NULL;
	mutex_unlock(&irix_spty_table_lock);
	ida_free(&irix_spty_ids, id);
	kfifo_free(&pt->to_master);
	kfifo_free(&pt->to_slave);
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

	pr_info("irix_spty: ptc clone open -> pty %u (/dev/tty%c%u)\n",
		pt->index, "qrstuvwxyz"[pt->index / 100], pt->index % 100);
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
	wake_up_all(&pt->master_wait);

	if (free_pair)
		irix_spty_release_pair(pt);
	return 0;
}

static ssize_t irix_ptc_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct irix_spty *pt = file->private_data;
	unsigned int copied = 0;
	int rc;

	if (!count)
		return 0;

	for (;;) {
		mutex_lock(&pt->lock);
		if (pt->pt_pkt && count) {
			unsigned char pkt = pt->pt_pkt;
			pt->pt_pkt = 0;
			mutex_unlock(&pt->lock);
			if (put_user(pkt, buf))
				return -EFAULT;
			return 1;
		}
		if (!kfifo_is_empty(&pt->to_master)) {
			if (pt->ptc_state & IRIX_PTC_PKT) {
				unsigned char zero = 0;
				if (put_user(zero, buf)) {
					mutex_unlock(&pt->lock);
					return -EFAULT;
				}
				buf++;
				count--;
				copied = 1;
				if (!count) {
					mutex_unlock(&pt->lock);
					return copied;
				}
			}
			rc = kfifo_to_user(&pt->to_master, buf, count, &copied);
			mutex_unlock(&pt->lock);
			wake_up_all(&pt->slave_wait);
			if (rc)
				return rc;
			return copied + ((pt->ptc_state & IRIX_PTC_PKT) ? 1 : 0);
		}
		if (pt->pts_state & IRIX_PT_DEAD) {
			mutex_unlock(&pt->lock);
			return 0;
		}
		mutex_unlock(&pt->lock);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		rc = wait_event_interruptible(pt->master_wait,
			irix_spty_master_readable(pt));
		if (rc)
			return rc;
	}
}

static ssize_t irix_ptc_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct irix_spty *pt = file->private_data;
	unsigned int copied = 0;
	int rc;

	mutex_lock(&pt->lock);
	/* spty.c drops controller M_DATA when no slave read queue exists. */
	if (!pt->slave_open) {
		mutex_unlock(&pt->lock);
		return count;
	}
	if (pt->pts_state & IRIX_PT_DEAD) {
		mutex_unlock(&pt->lock);
		return -EIO;
	}
	rc = kfifo_from_user(&pt->to_slave, buf, count, &copied);
	mutex_unlock(&pt->lock);
	if (copied)
		wake_up_all(&pt->slave_wait);
	if (rc)
		return rc;
	if (!copied && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;
	return copied;
}

static long irix_ptc_nested_ioctl(struct irix_spty *pt, unsigned int cmd,
				  unsigned long arg)
{
	int value;

	switch (cmd) {
	case IRIX_ISPTM:
		return 0;
	case IRIX_UNLKPT:
		pt->pts_state &= ~IRIX_PTS_LOCKED;
		wake_up_all(&pt->slave_wait);
		return 0;
	case IRIX_SVR4SOPEN:
		pt->pts_auto_ld = 0;
		return 0;
	case IRIX_TIOCPKT:
	case TIOCPKT:
		if (get_user(value, (int __user *)arg))
			return -EFAULT;
		if (value) {
			pt->ptc_state |= IRIX_PTC_PKT;
			pt->pt_pkt = TIOCPKT_DATA;
		} else {
			pt->ptc_state &= ~IRIX_PTC_PKT;
		}
		pt->ptc_state |= IRIX_PTC_PKT_SET;
		wake_up_all(&pt->master_wait);
		return 0;
	default:
		return -ENOTTY;
	}
}

static long irix_ptc_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct irix_spty *pt = file->private_data;
	struct irix_n32_strioctl si;
	long rc;

	if (!pt)
		return -ENODEV;

	mutex_lock(&pt->lock);
	if (cmd == IRIX_I_STR) {
		if (copy_from_user(&si, (void __user *)arg, sizeof(si))) {
			mutex_unlock(&pt->lock);
			return -EFAULT;
		}
		rc = irix_ptc_nested_ioctl(pt, si.ic_cmd,
			(unsigned long)(uintptr_t)si.ic_dp);
		mutex_unlock(&pt->lock);
		return rc;
	}

	switch (cmd) {
	case IRIX_ISPTM:
	case IRIX_UNLKPT:
	case IRIX_SVR4SOPEN:
	case IRIX_TIOCPKT:
		rc = irix_ptc_nested_ioctl(pt, cmd, arg);
		mutex_unlock(&pt->lock);
		return rc;
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
		pt->pt_pkt |= (pt->ptc_state & IRIX_PTC_PKT) ? TIOCPKT_IOCTL : 0;
		mutex_unlock(&pt->lock);
		wake_up_all(&pt->slave_wait);
		wake_up_all(&pt->master_wait);
		return 0;
	default:
		mutex_unlock(&pt->lock);
		return -ENOTTY;
	}
}

static __poll_t irix_ptc_poll(struct file *file, poll_table *wait)
{
	struct irix_spty *pt = file->private_data;
	__poll_t mask = EPOLLOUT | EPOLLWRNORM;

	if (!pt)
		return EPOLLERR;
	poll_wait(file, &pt->master_wait, wait);
	mutex_lock(&pt->lock);
	if (pt->pts_state & IRIX_PT_DEAD)
		mask |= EPOLLHUP;
	if (!kfifo_is_empty(&pt->to_master) || pt->pt_pkt)
		mask |= EPOLLIN | EPOLLRDNORM;
	mutex_unlock(&pt->lock);
	return mask;
}

static int irix_pts_open(struct inode *inode, struct file *file)
{
	unsigned int index = iminor(inode);
	struct irix_spty *pt;

	if (index >= maxpty)
		return -ENODEV;

	mutex_lock(&irix_spty_table_lock);
	pt = irix_spty_table[index];
	if (!pt) {
		mutex_unlock(&irix_spty_table_lock);
		return -ENODEV;
	}
	mutex_lock(&pt->lock);
	if (!pt->master_open || (pt->ptc_state & IRIX_PT_DEAD)) {
		mutex_unlock(&pt->lock);
		mutex_unlock(&irix_spty_table_lock);
		return -EIO;
	}
	if (pt->pts_state & IRIX_PTS_LOCKED) {
		mutex_unlock(&pt->lock);
		mutex_unlock(&irix_spty_table_lock);
		return -EACCES;
	}
	pt->slave_open++;
	pt->pts_state &= ~(IRIX_PT_UNBORN | IRIX_PT_DEAD);
	file->private_data = pt;
	mutex_unlock(&pt->lock);
	mutex_unlock(&irix_spty_table_lock);
	wake_up_all(&pt->master_wait);
	return 0;
}

static int irix_pts_release(struct inode *inode, struct file *file)
{
	struct irix_spty *pt = file->private_data;
	bool free_pair = false;

	if (!pt)
		return 0;
	mutex_lock(&pt->lock);
	if (pt->slave_open)
		pt->slave_open--;
	if (!pt->slave_open) {
		pt->pts_state |= IRIX_PT_DEAD;
		kfifo_reset(&pt->to_slave);
		free_pair = !pt->master_open;
	}
	mutex_unlock(&pt->lock);
	wake_up_all(&pt->master_wait);
	if (free_pair)
		irix_spty_release_pair(pt);
	return 0;
}

static ssize_t irix_pts_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct irix_spty *pt = file->private_data;
	unsigned int copied;
	int rc;

	for (;;) {
		mutex_lock(&pt->lock);
		if (!kfifo_is_empty(&pt->to_slave)) {
			rc = kfifo_to_user(&pt->to_slave, buf, count, &copied);
			mutex_unlock(&pt->lock);
			wake_up_all(&pt->master_wait);
			return rc ? rc : copied;
		}
		if (pt->ptc_state & IRIX_PT_DEAD) {
			mutex_unlock(&pt->lock);
			return 0;
		}
		mutex_unlock(&pt->lock);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		rc = wait_event_interruptible(pt->slave_wait,
			irix_spty_slave_readable(pt));
		if (rc)
			return rc;
	}
}

static ssize_t irix_pts_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct irix_spty *pt = file->private_data;
	unsigned int copied = 0;
	int rc;

	mutex_lock(&pt->lock);
	if (!pt->master_open || (pt->ptc_state & IRIX_PT_DEAD)) {
		mutex_unlock(&pt->lock);
		return -EIO;
	}
	rc = kfifo_from_user(&pt->to_master, buf, count, &copied);
	mutex_unlock(&pt->lock);
	if (copied)
		wake_up_all(&pt->master_wait);
	if (rc)
		return rc;
	if (!copied && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;
	return copied;
}

static long irix_pts_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct irix_spty *pt = file->private_data;

	mutex_lock(&pt->lock);
	switch (cmd) {
	case TIOCGWINSZ:
		if (copy_to_user((void __user *)arg, &pt->winsize,
				 sizeof(pt->winsize))) {
			mutex_unlock(&pt->lock);
			return -EFAULT;
		}
		break;
	case TIOCSWINSZ:
		if (copy_from_user(&pt->winsize, (void __user *)arg,
				   sizeof(pt->winsize))) {
			mutex_unlock(&pt->lock);
			return -EFAULT;
		}
		if (pt->ptc_state & IRIX_PTC_PKT)
			pt->pt_pkt |= TIOCPKT_IOCTL;
		wake_up_all(&pt->master_wait);
		break;
	default:
		mutex_unlock(&pt->lock);
		return -ENOTTY;
	}
	mutex_unlock(&pt->lock);
	return 0;
}

static __poll_t irix_pts_poll(struct file *file, poll_table *wait)
{
	struct irix_spty *pt = file->private_data;
	__poll_t mask = EPOLLOUT | EPOLLWRNORM;

	poll_wait(file, &pt->slave_wait, wait);
	mutex_lock(&pt->lock);
	if (pt->ptc_state & IRIX_PT_DEAD)
		mask |= EPOLLHUP;
	if (!kfifo_is_empty(&pt->to_slave))
		mask |= EPOLLIN | EPOLLRDNORM;
	mutex_unlock(&pt->lock);
	return mask;
}

static const struct file_operations irix_ptc_fops = {
	.owner          = THIS_MODULE,
	.open           = irix_ptc_open,
	.release        = irix_ptc_release,
	.read           = irix_ptc_read,
	.write          = irix_ptc_write,
	.unlocked_ioctl = irix_ptc_ioctl,
	.compat_ioctl   = irix_ptc_ioctl,
	.poll           = irix_ptc_poll,
	.llseek         = noop_llseek,
};

static const struct file_operations irix_pts_fops = {
	.owner          = THIS_MODULE,
	.open           = irix_pts_open,
	.release        = irix_pts_release,
	.read           = irix_pts_read,
	.write          = irix_pts_write,
	.unlocked_ioctl = irix_pts_ioctl,
	.compat_ioctl   = irix_pts_ioctl,
	.poll           = irix_pts_poll,
	.llseek         = noop_llseek,
};

static struct miscdevice irix_ptc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ptc",
	.fops = &irix_ptc_fops,
	.mode = 0666,
};

static int __init irix_spty_init(void)
{
	unsigned int i;
	int rc;

	if (!maxpty || maxpty > IRIX_SPTY_MAXPTY)
		return -EINVAL;

	rc = alloc_chrdev_region(&irix_pts_devt, 0, maxpty, "irix_pts");
	if (rc)
		return rc;
	cdev_init(&irix_pts_cdev, &irix_pts_fops);
	irix_pts_cdev.owner = THIS_MODULE;
	rc = cdev_add(&irix_pts_cdev, irix_pts_devt, maxpty);
	if (rc)
		goto err_region;

	irix_pts_class = class_create("irix_pts");
	if (IS_ERR(irix_pts_class)) {
		rc = PTR_ERR(irix_pts_class);
		goto err_cdev;
	}

	for (i = 0; i < maxpty; ++i) {
		char name[16];
		irix_slave_name(i, name, sizeof(name));
		if (IS_ERR(device_create(irix_pts_class, NULL,
				MKDEV(MAJOR(irix_pts_devt), i), NULL, "%s", name))) {
			rc = -ENODEV;
			goto err_devices;
		}
	}

	rc = misc_register(&irix_ptc_device);
	if (rc)
		goto err_devices;

	pr_info("irix_spty: IRIX PTC/PTS loaded, maxpty=%u, /dev/ptc + /dev/tty[q-z]N ready\n",
		maxpty);
	return 0;

err_devices:
	while (i--)
		device_destroy(irix_pts_class, MKDEV(MAJOR(irix_pts_devt), i));
	class_destroy(irix_pts_class);
err_cdev:
	cdev_del(&irix_pts_cdev);
err_region:
	unregister_chrdev_region(irix_pts_devt, maxpty);
	return rc;
}

static void __exit irix_spty_exit(void)
{
	unsigned int i;

	misc_deregister(&irix_ptc_device);
	for (i = 0; i < maxpty; ++i)
		device_destroy(irix_pts_class, MKDEV(MAJOR(irix_pts_devt), i));
	class_destroy(irix_pts_class);
	cdev_del(&irix_pts_cdev);
	unregister_chrdev_region(irix_pts_devt, maxpty);

	mutex_lock(&irix_spty_table_lock);
	for (i = 0; i < IRIX_SPTY_MAXPTY; ++i) {
		struct irix_spty *pt = irix_spty_table[i];
		if (!pt)
			continue;
		irix_spty_table[i] = NULL;
		ida_free(&irix_spty_ids, i);
		kfifo_free(&pt->to_master);
		kfifo_free(&pt->to_slave);
		kfree(pt);
	}
	mutex_unlock(&irix_spty_table_lock);
	ida_destroy(&irix_spty_ids);
	pr_info("irix_spty: unloaded\n");
}

module_init(irix_spty_init);
module_exit(irix_spty_exit);

MODULE_DESCRIPTION("IRIX System V PTC/PTS source port");
MODULE_AUTHOR("irix-re");
MODULE_LICENSE("GPL");
