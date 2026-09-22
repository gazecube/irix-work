// SPDX-License-Identifier: GPL-2.0
/*
 * IRIX System V pseudo-terminal controller/slave compatibility driver.
 *
 * Source-driven port of SGI IRIX kern/io/spty.c (Revision 3.62).
 * Linux supplies device, memory, locking and wait-queue primitives.  This
 * module is the kernel substrate for reproducing the IRIX PTC/PTS behavior;
 * STREAMS and line-discipline semantics are still being ported source-first.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/idr.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/signal.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>

#include "irix_spty.h"

static DEFINE_IDA(irix_spty_ids);
static DEFINE_MUTEX(irix_spty_table_lock);
static struct irix_spty *irix_spty_table[IRIX_SPTY_MAXPTY];
static unsigned int maxpty = IRIX_SPTY_MAXPTY;
static struct tty_driver *irix_pts_driver[10];
static char irix_pts_name[10][5];

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


static void irix_spty_echo_locked(struct irix_spty *pt, unsigned char ch)
{
	if (kfifo_avail(&pt->to_master))
		kfifo_in(&pt->to_master, &ch, 1);
}

/*
 * Source-driven subset of stty_ld input processing.  The important
 * architectural point is that controller M_DATA does not bypass the line
 * discipline: canonicalization and echo happen before data reaches the
 * slave stream head.
 */
static void irix_spty_input_char_locked(struct irix_spty *pt, unsigned char ch)
{
	u32 iflag = be32_to_cpu(pt->termio.c_iflag);
	u32 lflag = be32_to_cpu(pt->termio.c_lflag);
	unsigned char tmp;

	if (pt->literal_next) {
		pt->literal_next = 0;
		goto ordinary;
	}
	if ((lflag & IRIX_IEXTEN) && ch == pt->termio.c_cc[IRIX_VLNEXT]) {
		pt->literal_next = 1;
		if (lflag & IRIX_ECHO) {
			unsigned char vis[2] = {'^', '\b'};
			kfifo_in(&pt->to_master, vis,
				 min_t(unsigned int, 2, kfifo_avail(&pt->to_master)));
		}
		return;
	}
	if ((lflag & IRIX_IEXTEN) && ch == pt->termio.c_cc[IRIX_VDISCARD]) {
		/*
		 * stty_ld's VFLUSHO transition flushes pending output before
		 * entering FLUSHO; the next VDISCARD leaves discard mode.
		 */
		if (!pt->output_discard) {
			kfifo_reset(&pt->to_master);
			if (pt->ptc_state & IRIX_PTC_PKT)
				pt->pt_pkt |= TIOCPKT_FLUSHWRITE;
			pt->output_discard = 1;
		} else {
			pt->output_discard = 0;
		}
		wake_up_all(&pt->master_wait);
		wake_up_all(&pt->slave_wait);
		return;
	}

ordinary:
	if (iflag & IRIX_ISTRIP)
		ch &= 0x7f;
	if ((iflag & IRIX_IUCLC) && ch >= 'A' && ch <= 'Z')
		ch += 'a' - 'A';
	if (ch == '\n' && (iflag & IRIX_INLCR))
		ch = '\r';
	else if (ch == '\r') {
		if (iflag & IRIX_IGNCR)
			return;
		if (iflag & IRIX_ICRNL)
			ch = '\n';
	}

	if ((iflag & IRIX_IXON) && ch == pt->termio.c_cc[IRIX_VSTOP]) {
		pt->pts_state |= IRIX_PTS_TXSTOP;
		if (pt->ptc_state & IRIX_PTC_PKT)
			pt->pt_pkt |= TIOCPKT_STOP;
		return;
	}
	if ((iflag & IRIX_IXON) &&
	    (ch == pt->termio.c_cc[IRIX_VSTART] ||
	     ((iflag & IRIX_IXANY) && (pt->pts_state & IRIX_PTS_TXSTOP)))) {
		pt->pts_state &= ~IRIX_PTS_TXSTOP;
		if (pt->ptc_state & IRIX_PTC_PKT)
			pt->pt_pkt |= TIOCPKT_START;
		if (ch == pt->termio.c_cc[IRIX_VSTART])
			return;
	}

	if ((lflag & IRIX_ISIG) &&
	    (ch == pt->termio.c_cc[IRIX_VINTR] ||
	     ch == pt->termio.c_cc[IRIX_VQUIT] ||
	     ch == pt->termio.c_cc[IRIX_VSUSP] ||
	     ((lflag & IRIX_IEXTEN) && ch == pt->termio.c_cc[IRIX_VDSUSP]))) {
		pt->literal_next = 0;
		pt->output_discard = 0;
		if (!(lflag & IRIX_NOFLSH) &&
		    !((lflag & IRIX_IEXTEN) && ch == pt->termio.c_cc[IRIX_VDSUSP])) {
			kfifo_reset(&pt->to_slave);
			kfifo_reset(&pt->canon_input);
			kfifo_reset(&pt->to_master);
			if (pt->ptc_state & IRIX_PTC_PKT)
				pt->pt_pkt |= TIOCPKT_FLUSHREAD | TIOCPKT_FLUSHWRITE;
		}
		/*
		 * IRIX emits M_PCSIG (or M_SIG for VDSUSP) here. The Linux
		 * substrate must not signal current: the foreground tty pgrp
		 * belongs to the process/session personality and is wired there.
		 */
		return;
	}

	if (!(lflag & IRIX_ICANON) || pt->remote_mode) {
		if ((lflag & IRIX_ECHO) || ((lflag & IRIX_ECHONL) && ch == '\n'))
			irix_spty_echo_locked(pt, ch);
		if (kfifo_avail(&pt->to_slave))
			kfifo_in(&pt->to_slave, &ch, 1);
		return;
	}

	if (ch == pt->termio.c_cc[IRIX_VERASE]) {
		if (kfifo_len(&pt->canon_input)) {
			kfifo_out_peek(&pt->canon_input, &tmp, 1);
			/* kfifo has no tail-pop API; rebuild one byte shorter. */
			{
				unsigned int n = kfifo_len(&pt->canon_input), i;
				unsigned char *p = kmalloc(n, GFP_KERNEL);
				if (p) {
					kfifo_out(&pt->canon_input, p, n);
					for (i = 0; i + 1 < n; ++i)
						kfifo_in(&pt->canon_input, &p[i], 1);
					kfree(p);
				}
			}
			if ((lflag & (IRIX_ECHO|IRIX_ECHOE)) ==
			    (IRIX_ECHO|IRIX_ECHOE)) {
				unsigned char bs[3] = {'\b', ' ', '\b'};
				kfifo_in(&pt->to_master, bs, min_t(unsigned int, 3,
					kfifo_avail(&pt->to_master)));
			}
		}
		return;
	}

	if (ch == pt->termio.c_cc[IRIX_VKILL]) {
		kfifo_reset(&pt->canon_input);
		if (lflag & IRIX_ECHOK)
			irix_spty_echo_locked(pt, '\n');
		return;
	}

	if ((lflag & IRIX_IEXTEN) && ch == pt->termio.c_cc[IRIX_VWERASE]) {
		unsigned int n = kfifo_len(&pt->canon_input);
		unsigned char *p;
		if (!n)
			return;
		p = kmalloc(n, GFP_KERNEL);
		if (!p)
			return;
		kfifo_out(&pt->canon_input, p, n);
		while (n && (p[n - 1] == ' ' || p[n - 1] == '\t'))
			n--;
		while (n && p[n - 1] != ' ' && p[n - 1] != '\t')
			n--;
		kfifo_in(&pt->canon_input, p, n);
		kfree(p);
		return;
	}

	if ((lflag & IRIX_IEXTEN) && ch == pt->termio.c_cc[IRIX_VREPRINT]) {
		unsigned int n = kfifo_len(&pt->canon_input);
		unsigned char *p = kmalloc(n ? n : 1, GFP_KERNEL);
		irix_spty_echo_locked(pt, '\n');
		if (p) {
			kfifo_out_peek(&pt->canon_input, p, n);
			kfifo_in(&pt->to_master, p,
				 min_t(unsigned int, n, kfifo_avail(&pt->to_master)));
			kfree(p);
		}
		return;
	}

	if (ch == pt->termio.c_cc[IRIX_VEOF]) {
		while (!kfifo_is_empty(&pt->canon_input) &&
		       kfifo_avail(&pt->to_slave)) {
			kfifo_out(&pt->canon_input, &tmp, 1);
			kfifo_in(&pt->to_slave, &tmp, 1);
		}
		return;
	}

	if ((lflag & IRIX_ECHO) || ((lflag & IRIX_ECHONL) && ch == '\n'))
		irix_spty_echo_locked(pt, ch);
	if (kfifo_avail(&pt->canon_input))
		kfifo_in(&pt->canon_input, &ch, 1);

	if (ch == '\n' ||
	    (pt->termio.c_cc[IRIX_VEOL] && ch == pt->termio.c_cc[IRIX_VEOL]) ||
	    (pt->termio.c_cc[IRIX_VEOL2] && ch == pt->termio.c_cc[IRIX_VEOL2])) {
		while (!kfifo_is_empty(&pt->canon_input) &&
		       kfifo_avail(&pt->to_slave)) {
			kfifo_out(&pt->canon_input, &tmp, 1);
			kfifo_in(&pt->to_slave, &tmp, 1);
		}
	}
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
	init_waitqueue_head(&pt->drain_wait);
	tty_port_init(&pt->port);
	pt->index = id;
	pt->pts_state = IRIX_PT_UNBORN | IRIX_PTS_LOCKED;
	pt->ptc_state = IRIX_PT_UNBORN;
	pt->pts_auto_ld = 1;
	/* Exact defaults from IRIX stty_ld.c:def_stty_ld. */
	pt->termio.c_iflag = cpu_to_be32(IRIX_ICRNL | IRIX_IXON | IRIX_IXANY |
					 IRIX_BRKINT | IRIX_IGNPAR | IRIX_ISTRIP);
	pt->termio.c_oflag = cpu_to_be32(IRIX_OPOST | IRIX_ONLCR | IRIX_TAB3);
	pt->termio.c_lflag = cpu_to_be32(IRIX_ISIG | IRIX_ICANON | IRIX_ECHO |
					 IRIX_ECHOE | IRIX_ECHOK | IRIX_ECHOKE |
					 IRIX_ECHOCTL | IRIX_IEXTEN);
	pt->termio.c_line = 1; /* LDISC1 */
	pt->termio.c_cc[IRIX_VINTR] = 0177;
	pt->termio.c_cc[IRIX_VQUIT] = 034;
	pt->termio.c_cc[IRIX_VERASE] = '';
	pt->termio.c_cc[IRIX_VKILL] = 025;
	pt->termio.c_cc[IRIX_VEOF] = 004;
	pt->termio.c_cc[IRIX_VSTART] = 021;
	pt->termio.c_cc[IRIX_VSTOP] = 023;
	pt->termio.c_cc[IRIX_VREPRINT] = 022;
	pt->termio.c_cc[IRIX_VDISCARD] = 017;
	pt->termio.c_cc[IRIX_VWERASE] = 027;
	pt->termio.c_cc[IRIX_VLNEXT] = 026;

	rc = kfifo_alloc(&pt->to_master, IRIX_SPTY_FIFO_SIZE, GFP_KERNEL);
	if (rc)
		goto fail;
	rc = kfifo_alloc(&pt->to_slave, IRIX_SPTY_FIFO_SIZE, GFP_KERNEL);
	if (rc) {
		kfifo_free(&pt->to_master);
		goto fail;
	}
	rc = kfifo_alloc(&pt->canon_input, IRIX_SPTY_FIFO_SIZE, GFP_KERNEL);
	if (rc) {
		kfifo_free(&pt->to_slave);
		kfifo_free(&pt->to_master);
		goto fail;
	}

	mutex_lock(&irix_spty_table_lock);
	irix_spty_table[id] = pt;
	mutex_unlock(&irix_spty_table_lock);

	if (IS_ERR(tty_port_register_device(&pt->port,
		irix_pts_driver[id / IRIX_SPTY_PER_MAJOR],
		id % IRIX_SPTY_PER_MAJOR, NULL))) {
		rc = -ENODEV;
		mutex_lock(&irix_spty_table_lock);
		irix_spty_table[id] = NULL;
		mutex_unlock(&irix_spty_table_lock);
		kfifo_free(&pt->canon_input);
		kfifo_free(&pt->to_slave);
		kfifo_free(&pt->to_master);
		tty_port_destroy(&pt->port);
		goto fail;
	}
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
	tty_unregister_device(irix_pts_driver[id / IRIX_SPTY_PER_MAJOR],
		id % IRIX_SPTY_PER_MAJOR);
	tty_port_destroy(&pt->port);
	ida_free(&irix_spty_ids, id);
	kfifo_free(&pt->to_master);
	kfifo_free(&pt->to_slave);
	kfifo_free(&pt->canon_input);
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
	struct tty_struct *tty = NULL;

	if (!pt)
		return 0;

	mutex_lock(&pt->lock);
	pt->master_open = 0;
	pt->ptc_state |= IRIX_PT_DEAD;
	pt->ptc_state &= ~IRIX_PTC_QUEUED;
	memset(&pt->winsize, 0, sizeof(pt->winsize));
	free_pair = !pt->slave_open;
	if (pt->tty)
		tty = tty_kref_get(pt->tty);
	mutex_unlock(&pt->lock);
	wake_up_all(&pt->slave_wait);
	wake_up_all(&pt->master_wait);
	wake_up_all(&pt->drain_wait);
	if (tty) {
		tty_hangup(tty);
		tty_kref_put(tty);
	}

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
			unsigned int prefix = 0;

			if (pt->ptc_state & IRIX_PTC_PKT) {
				unsigned char zero = 0;
				if (put_user(zero, buf)) {
					mutex_unlock(&pt->lock);
					return -EFAULT;
				}
				buf++;
				count--;
				prefix = 1;
				if (!count) {
					mutex_unlock(&pt->lock);
					return prefix;
				}
			}
			rc = kfifo_to_user(&pt->to_master, buf, count, &copied);
			mutex_unlock(&pt->lock);
			wake_up_all(&pt->slave_wait);
			if (kfifo_is_empty(&pt->to_master))
				wake_up_all(&pt->drain_wait);
			if (rc)
				return rc;
			return prefix + copied;
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
	unsigned char chunk[256];
	size_t done = 0;

	/*
	 * The Linux tty core is now the slave stream head.  Feeding the flip
	 * buffer gives native children a real N_TTY line discipline, controlling
	 * tty, job control and signal semantics instead of duplicating them in
	 * this driver.
	 */
	while (done < count) {
		size_t n = min_t(size_t, sizeof(chunk), count - done);
		int pushed;

		if (copy_from_user(chunk, buf + done, n))
			return done ? (ssize_t)done : -EFAULT;
		mutex_lock(&pt->lock);
		if (!pt->slave_open || !pt->tty) {
			mutex_unlock(&pt->lock);
			return count;
		}
		if (pt->pts_state & IRIX_PT_DEAD) {
			mutex_unlock(&pt->lock);
			return done ? (ssize_t)done : -EIO;
		}
		mutex_unlock(&pt->lock);

		pushed = tty_insert_flip_string(&pt->port, chunk, n);
		if (!pushed)
			break;
		tty_flip_buffer_push(&pt->port);
		done += pushed;
	}
	return done;
}

static long irix_termio_ioctl(struct irix_spty *pt, unsigned int cmd,
			       unsigned long arg)
{
	struct irix_termio_wire tio;
	int value;

	switch (cmd) {
	case IRIX_TCGETA:
		tio.c_iflag = pt->termio.c_iflag;
		tio.c_oflag = pt->termio.c_oflag;
		tio.c_cflag = pt->termio.c_cflag;
		tio.c_lflag = pt->termio.c_lflag;
		tio.c_line = pt->termio.c_line;
		memcpy(tio.c_cc, pt->termio.c_cc, IRIX_NCCS);
		return copy_to_user((void __user *)arg, &tio, sizeof(tio)) ?
			-EFAULT : 0;
	case IRIX_TCSETAF:
		kfifo_reset(&pt->to_slave);
		kfifo_reset(&pt->canon_input);
		if (pt->ptc_state & IRIX_PTC_PKT)
			pt->pt_pkt |= TIOCPKT_FLUSHREAD;
		fallthrough;
	case IRIX_TCSETA:
	case IRIX_TCSETAW:
		if (copy_from_user(&tio, (void __user *)arg, sizeof(tio)))
			return -EFAULT;
		{
			u32 old_iflag = be32_to_cpu(pt->termio.c_iflag);
			u32 new_iflag = be32_to_cpu(tio.c_iflag);
			if ((old_iflag ^ new_iflag) & IRIX_IXON) {
				if (pt->ptc_state & IRIX_PTC_PKT) {
					pt->pt_pkt &= ~(TIOCPKT_DOSTOP | TIOCPKT_NOSTOP);
					pt->pt_pkt |= (new_iflag & IRIX_IXON) ?
						TIOCPKT_DOSTOP : TIOCPKT_NOSTOP;
				}
				if (!(new_iflag & IRIX_IXON))
					pt->pts_state &= ~IRIX_PTS_TXSTOP;
			}
		}
		pt->termio.c_iflag = tio.c_iflag;
		pt->termio.c_oflag = tio.c_oflag;
		pt->termio.c_cflag = tio.c_cflag;
		pt->termio.c_lflag = tio.c_lflag;
		pt->termio.c_line = tio.c_line;
		memcpy(pt->termio.c_cc, tio.c_cc, IRIX_NCCS);
		if (pt->ptc_state & IRIX_PTC_PKT)
			pt->pt_pkt |= TIOCPKT_IOCTL;
		wake_up_all(&pt->master_wait);
		wake_up_all(&pt->slave_wait);
		return 0;
	case IRIX_TCXONC:
		if (get_user(value, (int __user *)arg))
			return -EFAULT;
		if (value == 0) {
			pt->output_stopped = 1;
			pt->pts_state |= IRIX_PTS_TXSTOP;
			if (pt->ptc_state & IRIX_PTC_PKT)
				pt->pt_pkt |= TIOCPKT_STOP;
		} else if (value == 1) {
			pt->output_stopped = 0;
			pt->pts_state &= ~IRIX_PTS_TXSTOP;
			if (pt->ptc_state & IRIX_PTC_PKT)
				pt->pt_pkt |= TIOCPKT_START;
			wake_up_all(&pt->master_wait);
			wake_up_all(&pt->slave_wait);
		} else {
			return -EINVAL;
		}
		return 0;
	case IRIX_TIOCREMOTE:
		if (get_user(value, (int __user *)arg))
			return -EFAULT;
		/*
		 * IRIX sends MC_NO_CANON/MC_DO_CANON to stty_ld.  Model that
		 * state explicitly: remote mode bypasses canonical editing, and
		 * discard any half-built canonical record at the transition.
		 */
		if (pt->remote_mode != !!value)
			kfifo_reset(&pt->canon_input);
		pt->remote_mode = !!value;
		wake_up_all(&pt->slave_wait);
		return 0;
	case IRIX_TCSBRK:
		/*
		 * spty.c interprets the integer argument, not the pointer value.
		 * A zero break with BRKINT flushes both directions and raises
		 * SIGINT upstream.  Signal delivery is completed by the process
		 * personality; reproduce the queue and packet effects here.
		 */
		if (get_user(value, (int __user *)arg))
			return -EFAULT;
		if (value == 0 &&
		    (be32_to_cpu(pt->termio.c_iflag) & (IRIX_IGNBRK | IRIX_BRKINT))
		      == IRIX_BRKINT) {
			kfifo_reset(&pt->to_slave);
			kfifo_reset(&pt->canon_input);
			kfifo_reset(&pt->to_master);
			if (pt->ptc_state & IRIX_PTC_PKT)
				pt->pt_pkt |= TIOCPKT_FLUSHREAD | TIOCPKT_FLUSHWRITE;
			wake_up_all(&pt->master_wait);
			wake_up_all(&pt->slave_wait);
		}
		return 0;
	default:
		return -ENOTTY;
	}
}

static long irix_ptc_nested_ioctl(struct irix_spty *pt, unsigned int cmd,
				  unsigned long arg)
{
	int value;

	switch (cmd) {
	case IRIX_TCGETA:
	case IRIX_TCSETA:
	case IRIX_TCSETAW:
	case IRIX_TCSETAF:
	case IRIX_TCXONC:
	case IRIX_TIOCREMOTE:
	case IRIX_TCSBRK:
		return irix_termio_ioctl(pt, cmd, arg);
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

	if (cmd == IRIX_SPTY_HOST_GET_ID) {
		struct irix_spty_host_id id = {
			.index = pt->index,
			.side = IRIX_SPTY_SIDE_MASTER,
		};
		return copy_to_user((void __user *)arg, &id, sizeof(id)) ?
			-EFAULT : 0;
	}

	mutex_lock(&pt->lock);
	if (cmd == IRIX_I_STR) {
		if (copy_from_user(&si, (void __user *)arg, sizeof(si))) {
			mutex_unlock(&pt->lock);
			return -EFAULT;
		}
		if (si.ic_cmd == IRIX_TCSETAW || si.ic_cmd == IRIX_TCSETAF) {
			mutex_unlock(&pt->lock);
			rc = wait_event_interruptible(pt->drain_wait,
				kfifo_is_empty(&pt->to_master) ||
				(pt->ptc_state & IRIX_PT_DEAD));
			if (rc)
				return rc;
			mutex_lock(&pt->lock);
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
	case IRIX_TCGETA:
	case IRIX_TCSETA:
	case IRIX_TCXONC:
	case IRIX_TIOCREMOTE:
	case IRIX_TCSBRK:
		rc = irix_termio_ioctl(pt, cmd, arg);
		mutex_unlock(&pt->lock);
		return rc;
	case IRIX_TCSETAW:
	case IRIX_TCSETAF:
		mutex_unlock(&pt->lock);
		rc = wait_event_interruptible(pt->drain_wait,
			kfifo_is_empty(&pt->to_master) ||
			(pt->ptc_state & IRIX_PT_DEAD));
		if (rc)
			return rc;
		mutex_lock(&pt->lock);
		rc = irix_termio_ioctl(pt, cmd, arg);
		mutex_unlock(&pt->lock);
		return rc;
	case IRIX_TIOCGWINSZ:
		if (copy_to_user((void __user *)arg, &pt->winsize,
				 sizeof(pt->winsize))) {
			mutex_unlock(&pt->lock);
			return -EFAULT;
		}
		mutex_unlock(&pt->lock);
		return 0;
	case IRIX_TIOCSWINSZ:
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

static struct irix_spty *irix_tty_pair(struct tty_struct *tty)
{
	unsigned int group = (unsigned long)tty->driver->driver_state;
	unsigned int id = group * IRIX_SPTY_PER_MAJOR + tty->index;
	struct irix_spty *pt = NULL;

	if (id >= maxpty)
		return NULL;
	mutex_lock(&irix_spty_table_lock);
	pt = irix_spty_table[id];
	mutex_unlock(&irix_spty_table_lock);
	return pt;
}

static int irix_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct irix_spty *pt = irix_tty_pair(tty);
	if (!pt)
		return -ENODEV;
	tty->driver_data = pt;
	return tty_port_install(&pt->port, driver, tty);
}

static int irix_tty_open(struct tty_struct *tty, struct file *file)
{
	struct irix_spty *pt = tty->driver_data;
	int rc;

	if (!pt)
		return -ENODEV;
	mutex_lock(&pt->lock);
	if (!pt->master_open || (pt->ptc_state & IRIX_PT_DEAD)) {
		mutex_unlock(&pt->lock);
		return -EIO;
	}
	if (pt->pts_state & IRIX_PTS_LOCKED) {
		mutex_unlock(&pt->lock);
		return -EACCES;
	}
	mutex_unlock(&pt->lock);

	rc = tty_port_open(&pt->port, tty, file);
	if (rc)
		return rc;
	mutex_lock(&pt->lock);
	pt->slave_open++;
	pt->tty = tty;
	pt->pts_state &= ~(IRIX_PT_UNBORN | IRIX_PT_DEAD);
	mutex_unlock(&pt->lock);
	wake_up_all(&pt->master_wait);
	return 0;
}

static void irix_tty_close(struct tty_struct *tty, struct file *file)
{
	struct irix_spty *pt = tty->driver_data;
	bool free_pair = false;

	if (!pt)
		return;
	tty_port_close(&pt->port, tty, file);
	mutex_lock(&pt->lock);
	if (pt->slave_open)
		pt->slave_open--;
	if (!pt->slave_open) {
		pt->tty = NULL;
		pt->pts_state |= IRIX_PT_DEAD;
		free_pair = !pt->master_open;
	}
	mutex_unlock(&pt->lock);
	wake_up_all(&pt->master_wait);
	wake_up_all(&pt->drain_wait);
	if (free_pair)
		irix_spty_release_pair(pt);
}

static ssize_t irix_tty_write(struct tty_struct *tty, const u8 *buf, size_t count)
{
	struct irix_spty *pt = tty->driver_data;
	unsigned int n;

	if (!pt)
		return -ENODEV;
	mutex_lock(&pt->lock);
	if (!pt->master_open || (pt->ptc_state & IRIX_PT_DEAD)) {
		mutex_unlock(&pt->lock);
		return -EIO;
	}
	n = min_t(unsigned int, count, kfifo_avail(&pt->to_master));
	kfifo_in(&pt->to_master, buf, n);
	mutex_unlock(&pt->lock);
	if (n)
		wake_up_all(&pt->master_wait);
	return n;
}

static unsigned int irix_tty_write_room(struct tty_struct *tty)
{
	struct irix_spty *pt = tty->driver_data;
	unsigned int n = 0;
	if (!pt)
		return 0;
	mutex_lock(&pt->lock);
	n = kfifo_avail(&pt->to_master);
	mutex_unlock(&pt->lock);
	return n;
}

static unsigned int irix_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct irix_spty *pt = tty->driver_data;
	unsigned int n = 0;
	if (!pt)
		return 0;
	mutex_lock(&pt->lock);
	n = kfifo_len(&pt->to_master);
	mutex_unlock(&pt->lock);
	return n;
}

static void irix_tty_flush_buffer(struct tty_struct *tty)
{
	struct irix_spty *pt = tty->driver_data;
	if (!pt)
		return;
	mutex_lock(&pt->lock);
	kfifo_reset(&pt->to_master);
	if (pt->ptc_state & IRIX_PTC_PKT)
		pt->pt_pkt |= TIOCPKT_FLUSHWRITE;
	mutex_unlock(&pt->lock);
	wake_up_all(&pt->master_wait);
	wake_up_all(&pt->drain_wait);
}

static const struct tty_operations irix_tty_ops = {
	.install = irix_tty_install,
	.open = irix_tty_open,
	.close = irix_tty_close,
	.write = irix_tty_write,
	.write_room = irix_tty_write_room,
	.chars_in_buffer = irix_tty_chars_in_buffer,
	.flush_buffer = irix_tty_flush_buffer,
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

	for (i = 0; i < 10; ++i) {
		struct tty_driver *drv;
		unsigned int first = i * IRIX_SPTY_PER_MAJOR;
		unsigned int lines;

		if (first >= maxpty)
			break;
		lines = min_t(unsigned int, IRIX_SPTY_PER_MAJOR, maxpty - first);
		drv = tty_alloc_driver(lines,
			TTY_DRIVER_DYNAMIC_DEV | TTY_DRIVER_RESET_TERMIOS);
		if (IS_ERR(drv)) {
			rc = PTR_ERR(drv);
			goto err_drivers;
		}
		irix_pts_driver[i] = drv;
		snprintf(irix_pts_name[i], sizeof(irix_pts_name[i]), "tty%c",
			"qrstuvwxyz"[i]);
		drv->driver_name = "irix_spty";
		drv->name = irix_pts_name[i];
		drv->major = 0;
		drv->minor_start = 0;
		drv->type = TTY_DRIVER_TYPE_PTY;
		drv->subtype = PTY_TYPE_SLAVE;
		drv->driver_state = (void *)(unsigned long)i;
		drv->init_termios = tty_std_termios;
		drv->init_termios.c_iflag = BRKINT | ICRNL | IXON | IXANY;
		drv->init_termios.c_oflag = OPOST | ONLCR;
		drv->init_termios.c_lflag =
			ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
		tty_set_operations(drv, &irix_tty_ops);
		rc = tty_register_driver(drv);
		if (rc) {
			put_tty_driver(drv);
			irix_pts_driver[i] = NULL;
			goto err_drivers;
		}
	}

	rc = misc_register(&irix_ptc_device);
	if (rc)
		goto err_drivers;
	pr_info("irix_spty: IRIX PTC + Linux tty-backed PTS loaded, maxpty=%u\n",
		maxpty);
	return 0;

err_drivers:
	while (i--) {
		if (!irix_pts_driver[i])
			continue;
		tty_unregister_driver(irix_pts_driver[i]);
		put_tty_driver(irix_pts_driver[i]);
		irix_pts_driver[i] = NULL;
	}
	return rc;
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
		tty_unregister_device(irix_pts_driver[i / IRIX_SPTY_PER_MAJOR],
			i % IRIX_SPTY_PER_MAJOR);
		tty_port_destroy(&pt->port);
		ida_free(&irix_spty_ids, i);
		kfifo_free(&pt->to_master);
		kfifo_free(&pt->to_slave);
		kfifo_free(&pt->canon_input);
		kfree(pt);
	}
	mutex_unlock(&irix_spty_table_lock);
	ida_destroy(&irix_spty_ids);

	for (i = 0; i < 10; ++i) {
		if (!irix_pts_driver[i])
			continue;
		tty_unregister_driver(irix_pts_driver[i]);
		put_tty_driver(irix_pts_driver[i]);
		irix_pts_driver[i] = NULL;
	}
	pr_info("irix_spty: unloaded\n");
}

module_init(irix_spty_init);
module_exit(irix_spty_exit);

MODULE_DESCRIPTION("IRIX System V PTC/PTS source port");
MODULE_AUTHOR("irix-re");
MODULE_LICENSE("GPL");
