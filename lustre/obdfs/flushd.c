/*
 * OBDFS Super operations - also used for Lustre file system
 *
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 * Copryright (C) 1999 Stelias Computing Inc. <braam@stelias.com>
 * Copryright (C) 1999 Seagate Technology Inc.
 *
 */
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <linux/locks.h>
#include <linux/errno.h>
#include <linux/swap.h>
#include <linux/smp_lock.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/sysrq.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/quotaops.h>
#include <linux/iobuf.h>
#include <linux/highmem.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>
#include <asm/mmu_context.h>

#include <linux/obd_support.h>
#include <linux/obd_class.h>
#include <linux/obdfs.h>


struct {
	int nfract;  /* Percentage of buffer cache dirty to 
			activate bdflush */
	int ndirty;  /* Maximum number of dirty blocks to write out per
			wake-cycle */
	int nrefill; /* Number of clean buffers to try to obtain
				each time we call refill */
	int nref_dirt; /* Dirty buffer threshold for activating bdflush
			  when trying to refill buffers. */
	int interval; /* jiffies delay between pupdate flushes */
	int age_buffer;  /* Time for normal buffer to age before we flush it */
	int age_super;  /* Time for superblock to age before we flush it */
} pupd_prm = {40, 500, 64, 256, 3*HZ, 30*HZ, 5*HZ };

/* Called with the superblock list lock */
static int obdfs_enqueue_pages(struct inode *inode, struct obdo **obdo,
			       int nr_slots, struct page **pages, char **bufs,
			       obd_size *counts, obd_off *offsets,
			       obd_flag *flag, int check_time)
{
	struct list_head *page_list = obdfs_iplist(inode);
	struct list_head *tmp;
	int num = 0;

	ENTRY;

	tmp = page_list;
	/* Traverse list in reverse order, so we do FIFO, not LIFO order */
	while ( (tmp = tmp->prev) != page_list && num < nr_slots ) {
		struct obdfs_pgrq *req;
		struct page *page;
		
		req = list_entry(tmp, struct obdfs_pgrq, rq_plist);
		page = req->rq_page;

		
		if (check_time && 
		    (jiffies - req->rq_jiffies) < pupd_prm.age_buffer)
			break;		/* pages are in chronological order */

		/* Only allocate the obdo if we will actually do I/O here */
		if ( !*obdo ) {
			OIDEBUG(inode);
			*obdo = obdo_fromid(IID(inode), inode->i_ino,
					    OBD_MD_FLNOTOBD);
			if ( IS_ERR(*obdo) ) {
				int err = PTR_ERR(*obdo);
				*obdo = NULL;

				EXIT;
				return err;
			}

			/* FIXME revisit fromid & from_inode */
			obdfs_from_inode(*obdo, inode);
			*flag = OBD_BRW_CREATE;
		}

		/* Remove request from list before write to avoid conflict.
		 * Note that obdfs_pgrq_del() also deletes the request.
		 */
		obdfs_pgrq_del(req);
		if ( !page ) {
			CDEBUG(D_CACHE, "no page \n");
			continue;
		}

		bufs[num] = (char *)page_address(page);
		pages[num] = page;
		counts[num] = PAGE_SIZE;
		offsets[num] = ((obd_off)page->index) << PAGE_SHIFT;
		CDEBUG(D_INFO, "ENQ inode %ld, page %p addr %p to vector\n", 
		       inode->i_ino, page, (char *)page_address(page));
		num++;
	}

	if (!list_empty(page_list))
		CDEBUG(D_CACHE, "inode %ld list not empty\n", inode->i_ino);
	CDEBUG(D_INFO, "added %d page(s) to vector\n", num);

	EXIT;
	return num;  
} /* obdfs_enqueue_pages */

/* Remove writeback requests for the superblock */
int obdfs_flush_reqs(struct list_head *inode_list, int check_time)
{
	struct list_head *tmp;
	int		  total_io = 0;
	obd_count	  num_io;
	obd_count         num_obdos;
	struct inode	 *inodes[MAX_IOVEC];	/* write data back to these */
	struct page	 *pages[MAX_IOVEC];	/* call put_page on these */
	struct obdo	 *obdos[MAX_IOVEC];
	char		 *bufs[MAX_IOVEC];
	obd_size	  counts[MAX_IOVEC];
	obd_off		  offsets[MAX_IOVEC];
	obd_flag	  flags[MAX_IOVEC];
	obd_count         bufs_per_obdo[MAX_IOVEC];
	int		  err = 0;
	struct obdfs_sb_info *sbi;

	ENTRY;
	if (!inode_list) {
		CDEBUG(D_INODE, "no list\n");
		EXIT;
		return 0;
	}

	sbi = list_entry(inode_list, struct obdfs_sb_info, osi_inodes);

	obd_down(&sbi->osi_list_mutex);
	if ( list_empty(inode_list) ) {
		CDEBUG(D_CACHE, "list empty\n");
		obd_up(&sbi->osi_list_mutex);
		EXIT;
		return 0;
	}

	/* Add each inode's dirty pages to a write vector, and write it.
	 * Traverse list in reverse order, so we do FIFO, not LIFO order
	 */
 again:
	tmp = inode_list;
	num_io = 0;
	num_obdos = 0;
	while ( (tmp = tmp->prev) != inode_list && total_io < pupd_prm.ndirty) {
		struct obdfs_inode_info *ii;
		struct inode *inode;
		int res;

		ii = list_entry(tmp, struct obdfs_inode_info, oi_inodes);
		inode = list_entry(ii, struct inode, u);
		inodes[num_obdos] = inode;
		obdos[num_obdos] = NULL;
		CDEBUG(D_INFO, "checking inode %ld pages\n", inode->i_ino);

		/* Make sure we reference "inode" and not "inodes[num_obdos]",
		 * as num_obdos will change after the loop is run.
		 */
		if (!list_empty(obdfs_iplist(inode))) {
			res = obdfs_enqueue_pages(inode, &obdos[num_obdos],
						  MAX_IOVEC - num_io,
						  &pages[num_io], &bufs[num_io],
						  &counts[num_io],
						  &offsets[num_io],
						  &flags[num_obdos],
						  check_time);
			CDEBUG(D_CACHE, "FLUSH inode %ld, pages flushed: %d\n",
			       inode->i_ino, res);
			if ( res < 0 ) {
				CDEBUG(D_INODE,
				       "fatal: unable to enqueue inode %ld (err %d)\n",
				       inode->i_ino, err);
				/* XXX Move bad inode to end of list so we can
				 * continue with flushing list.  This is a
				 * temporary measure to avoid machine lockups.
				 */
				list_del(tmp);
				list_add(tmp, inode_list);
				err = res;
				EXIT;
				goto BREAK;
			} else if (res) {
				num_io += res;
				total_io += res;
				bufs_per_obdo[num_obdos] = res;
				num_obdos++;
			}

			if ( num_io == MAX_IOVEC ) {
				obd_up(&sbi->osi_list_mutex);
				err = obdfs_do_vec_wr(inodes, num_io, num_obdos,
						      obdos, bufs_per_obdo,
						      pages, bufs, counts,
						      offsets, flags);
				if ( err ) {
					CDEBUG(D_INODE,
						"fatal: unable to do vec_wr (err %d)\n", err);
					EXIT;
					goto ERR;
				}
				obd_down(&sbi->osi_list_mutex);
				goto again;
			}
		}
	}

BREAK:
	obd_up(&sbi->osi_list_mutex);

	/* flush any remaining I/Os */
	if ( num_io ) {
		err = obdfs_do_vec_wr(inodes, num_io, num_obdos, obdos,
				      bufs_per_obdo, pages, bufs, counts,
				      offsets, flags);
		if (err)
			CDEBUG(D_INODE, "fatal: unable to do vec_wr (err %d)\n", err);
		num_io = 0;
		num_obdos = 0;
	}

	/* Remove inode from superblock dirty list when no more pages.
	 * Make sure we don't point at the current inode with tmp
	 * when we re-init the list on the inode, or we will loop.
	 */
	obd_down(&sbi->osi_list_mutex);
	tmp = inode_list;
	while ( (tmp = tmp->prev) != inode_list ) {
		struct obdfs_inode_info *ii;
		struct inode *inode;

		ii = list_entry(tmp, struct obdfs_inode_info, oi_inodes);
		inode = list_entry(ii, struct inode, u);
		CDEBUG(D_INFO, "checking inode %ld empty\n", inode->i_ino);
		if (list_empty(obdfs_iplist(inode))) {
			CDEBUG(D_CACHE, "remove inode %ld from dirty list\n",
			       inode->i_ino);
			tmp = tmp->next;
			list_del(obdfs_islist(inode));
			/* decrement inode reference for page cache */
			inode->i_count--;
			INIT_LIST_HEAD(obdfs_islist(inode));
		}
	}
	obd_up(&sbi->osi_list_mutex);

	CDEBUG(D_INFO, "flushed %d pages in total\n", total_io);
	EXIT;
ERR:
	return err;
} /* obdfs_flush_reqs */


void obdfs_flush_dirty_pages(int check_time)
{
	struct list_head *sl;

	ENTRY;
	sl = &obdfs_super_list;
	while ( (sl = sl->prev) != &obdfs_super_list ) {
		struct obdfs_sb_info *sbi = 
			list_entry(sl, struct obdfs_sb_info, osi_list);

		/* walk write requests here, use the sb, check the time */
		obdfs_flush_reqs(&sbi->osi_inodes, check_time);
	}
	EXIT;
} /* obdfs_flush_dirty_pages */


static struct task_struct *pupdated;

static int pupdate(void *unused) 
{
	struct task_struct * tsk = current;
	int interval;
	
	pupdated = current;

	exit_files(current);
	exit_mm(current);

	tsk->session = 1;
	tsk->pgrp = 1;
	sprintf(tsk->comm, "pupdated");
	pupdated = current;

	MOD_INC_USE_COUNT;	/* XXX until send_sig works */
	printk("pupdated activated...\n");

	/* sigstop and sigcont will stop and wakeup pupdate */
	spin_lock_irq(&tsk->sigmask_lock);
	sigfillset(&tsk->blocked);
	siginitsetinv(&tsk->blocked, sigmask(SIGTERM));
	recalc_sigpending(tsk);
	spin_unlock_irq(&tsk->sigmask_lock);

	for (;;) {
		/* update interval */
		interval = pupd_prm.interval;
		if (interval)
		{
			tsk->state = TASK_INTERRUPTIBLE;
			schedule_timeout(interval);
		}
		else
		{
		stop_pupdate:
			tsk->state = TASK_STOPPED;
			MOD_DEC_USE_COUNT; /* XXX until send_sig works */
			printk("pupdated stopped...\n");
			return 0;
		}
		/* check for sigstop */
		if (signal_pending(tsk))
		{
			int stopped = 0;
			spin_lock_irq(&tsk->sigmask_lock);
			if (sigismember(&tsk->signal, SIGTERM))
			{
				sigdelset(&tsk->signal, SIGTERM);
				stopped = 1;
			}
			recalc_sigpending(tsk);
			spin_unlock_irq(&tsk->sigmask_lock);
			if (stopped)
				goto stop_pupdate;
		}
		/* asynchronous setattr etc for the future ...
		flush_inodes();
		 */
		obdfs_flush_dirty_pages(1); 
	}
}


int obdfs_flushd_init(void)
{
	/*
	kernel_thread(bdflush, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	 */
	kernel_thread(pupdate, NULL, 0);
	CDEBUG(D_PSDEV, __FUNCTION__ ": flushd inited\n");
	return 0;
}

int obdfs_flushd_cleanup(void)
{
	ENTRY;
	/* deliver a signal to pupdated to shut it down
	   XXX need to kill it from user space for now XXX
	if (pupdated) {
		send_sig_info(SIGTERM, 1, pupdated);
	}
	 */

	EXIT;
	/* not reached */
	return 0;

}
