/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *  Copyright (C) 2001-2003 Cluster File Systems, Inc.
 *   Author: Andreas Dilger <adilger@clusterfs.com>
 *
 *   This file is part of Lustre, http://www.lustre.org.
 *
 *   Lustre is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Lustre is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Lustre; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * OST<->MDS recovery logging infrastructure.
 *
 * Invariants in implementation:
 * - we do not share logs among different OST<->MDS connections, so that
 *   if an OST or MDS fails it need only look at log(s) relevant to itself
 */

#define DEBUG_SUBSYSTEM S_LOG

#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif

#include <linux/fs.h>
#include <linux/obd_class.h>
#include <linux/lustre_log.h>
#include <portals/list.h>
#include <linux/lvfs.h>
#include <linux/obd_ost.h>

static int llog_lvfs_pad(struct l_file *file, int len, int index)
{
        struct llog_rec_hdr rec;
        struct llog_rec_tail tail;
        int rc;
        ENTRY;
        
        LASSERT(len >= LLOG_MIN_REC_SIZE && (len & 0xf) == 0);

        tail.lrt_len = rec.lrh_len = len;
        tail.lrt_index = rec.lrh_index = index;
        rec.lrh_type = 0;

        rc = lustre_fwrite(file, &rec, sizeof(rec), &file->f_pos);
        if (rc != sizeof(rec)) {
                CERROR("error writing padding record: rc %d\n", rc);
                GOTO(out, rc < 0 ? rc : rc = -EIO);
        }
        
        file->f_pos += len - sizeof(rec) - sizeof(tail);
        rc = lustre_fwrite(file, &tail, sizeof(tail), &file->f_pos);
        if (rc != sizeof(tail)) {
                CERROR("error writing padding record: rc %d\n", rc);
                GOTO(out, rc < 0 ? rc : rc = -EIO);
        }
        rc = 0;
 out: 
        RETURN(rc);
}

static int llog_lvfs_write_blob(struct l_file *file, struct llog_rec_hdr *rec,
                                void *buf, loff_t off)
{
        int rc;
        struct llog_rec_tail end;
        loff_t saved_off = file->f_pos;
        int buflen;

        ENTRY;
        file->f_pos = off;

        if (!buf) {
                rc = lustre_fwrite(file, rec, rec->lrh_len, &file->f_pos);
                if (rc != rec->lrh_len) {
                        CERROR("error writing log record: rc %d\n", rc);
                        GOTO(out, rc < 0 ? rc : rc = -ENOSPC);
                }
                GOTO(out, rc = 0);
        }

        /* the buf case */
        buflen = rec->lrh_len;
        rec->lrh_len = sizeof(*rec) + size_round(buflen) + sizeof(end);
        rc = lustre_fwrite(file, rec, sizeof(*rec), &file->f_pos);
        if (rc != sizeof(*rec)) {
                CERROR("error writing log hdr: rc %d\n", rc);
                GOTO(out, rc < 0 ? rc : rc = -ENOSPC);
        }

        rc = lustre_fwrite(file, buf, buflen, &file->f_pos);
        if (rc != buflen) {
                CERROR("error writing log buffer: rc %d\n", rc);
                GOTO(out, rc < 0 ? rc : rc  = -ENOSPC);
        }

        file->f_pos += size_round(buflen) - buflen;
        end.lrt_len = rec->lrh_len;
        end.lrt_index = rec->lrh_index;
        rc = lustre_fwrite(file, &end, sizeof(end), &file->f_pos);
        if (rc != sizeof(end)) {
                CERROR("error writing log tail: rc %d\n", rc);
                GOTO(out, rc < 0 ? rc : rc =  -ENOSPC);
        }

        rc = 0;
 out: 
        if (saved_off > file->f_pos)
                file->f_pos = saved_off;
        LASSERT(rc <= 0);
        RETURN(rc);
}

static int llog_lvfs_read_blob(struct l_file *file, void *buf, int size,
                               loff_t off)
{
        loff_t offset = off;
        int rc;
        ENTRY;

        rc = lustre_fread(file, buf, size, &offset);
        if (rc != size) {
                CERROR("error reading log record: rc %d\n", rc);
                RETURN(-EIO);
        }
        RETURN(0);
}

static int llog_lvfs_read_header(struct llog_handle *handle)
{
        struct llog_rec_tail tail;
        int rc;
        ENTRY;

        LASSERT(sizeof(*handle->lgh_hdr) == LLOG_CHUNK_SIZE);

        if (handle->lgh_file->f_dentry->d_inode->i_size == 0) {
                CERROR("not reading header from 0-byte log\n");
                RETURN(LLOG_EEMPTY);
        }

        rc = llog_lvfs_read_blob(handle->lgh_file, handle->lgh_hdr,
                                 LLOG_CHUNK_SIZE, 0);
        if (rc)
                CERROR("error reading log header\n");

        rc = llog_lvfs_read_blob(handle->lgh_file, &tail, sizeof(tail),
                                 handle->lgh_file->f_dentry->d_inode->i_size -
                                 sizeof(tail));
        if (rc)
                CERROR("error reading log tail\n");

        handle->lgh_last_idx = tail.lrt_index;
        handle->lgh_file->f_pos = handle->lgh_file->f_dentry->d_inode->i_size;

        RETURN(rc);
}

/* returns negative in on error; 0 if success && reccookie == 0; 1 otherwise */
/* appends if idx == -1, otherwise overwrites record idx. */
static int llog_lvfs_write_rec(struct llog_handle *loghandle,
                               struct llog_rec_hdr *rec,
                               struct llog_cookie *reccookie, int cookiecount, 
                               void *buf, int idx)
{
        struct llog_log_hdr *llh;
        int reclen = rec->lrh_len, index, rc;
        struct llog_rec_tail *lrt;
        struct file *file;
        loff_t offset;
        size_t left;
        ENTRY;

        llh = loghandle->lgh_hdr;
        file = loghandle->lgh_file;

        if (idx != -1) { 
                loff_t saved_offset;

                /* no header: only allowed to insert record 1 */
                if (idx != 1 && !file->f_dentry->d_inode->i_size) {
                        CERROR("idx != -1 in empty log\n");
                        LBUG();
                }

                if (loghandle->lgh_hdr->llh_size &&
                    loghandle->lgh_hdr->llh_size != rec->lrh_len)
                        RETURN(-EINVAL);

                rc = llog_lvfs_write_blob(file, &llh->llh_hdr, NULL, 0);
                /* we are done if we only write the header or on error */
                if (rc || idx == 0)
                        RETURN(rc);

                saved_offset = sizeof(*llh) + (idx-1) * rec->lrh_len;
                rc = llog_lvfs_write_blob(file, rec, buf, saved_offset);
                RETURN(rc);
        }

        /* Make sure that records don't cross a chunk boundary, so we can
         * process them page-at-a-time if needed.  If it will cross a chunk
         * boundary, write in a fake (but referenced) entry to pad the chunk.
         *
         * We know that llog_current_log() will return a loghandle that is
         * big enough to hold reclen, so all we care about is padding here.
         */
        left = LLOG_CHUNK_SIZE - (file->f_pos & (LLOG_CHUNK_SIZE - 1));
        if (buf) 
                reclen = sizeof(*rec) + size_round(rec->lrh_len) + 
                        sizeof(struct llog_rec_tail);

        /* NOTE: padding is a record, but no bit is set */
        if (left != 0 && left < reclen) {
                loghandle->lgh_last_idx++;
                rc = llog_lvfs_pad(file, left, loghandle->lgh_last_idx);
                if (rc)
                        RETURN(rc);
        }

        loghandle->lgh_last_idx++;
        index = loghandle->lgh_last_idx;
        rec->lrh_index = index;
        lrt = (void *)rec + rec->lrh_len - sizeof(*lrt);
        lrt->lrt_len = rec->lrh_len;
        lrt->lrt_index = rec->lrh_index;
        if (ext2_set_bit(index, llh->llh_bitmap)) {
                CERROR("argh, index %u already set in log bitmap?\n", index);
                LBUG(); /* should never happen */
        }
        llh->llh_count++;

        offset = 0;
        rc = llog_lvfs_write_blob(file, &llh->llh_hdr, NULL, 0);
        if (rc)
                RETURN(rc);

        rc = llog_lvfs_write_blob(file, rec, buf, file->f_pos);
        if (rc)
                RETURN(rc);

        CDEBUG(D_HA, "added record "LPX64": idx: %u, %u bytes\n",
               loghandle->lgh_id.lgl_oid, index, rec->lrh_len);
        if (rc == 0 && reccookie) {
                reccookie->lgc_lgl = loghandle->lgh_id;
                reccookie->lgc_index = index;
                rc = 1;
        }
        RETURN(rc);
}

/* We can skip reading at least as many log blocks as the number of
* minimum sized log records we are skipping.  If it turns out
* that we are not far enough along the log (because the
* actual records are larger than minimum size) we just skip
* some more records. */

static void llog_skip_over(__u64 *off, int curr, int goal)
{
        if (goal <= curr)
                return;
        *off = (*off + (goal-curr-1) * LLOG_MIN_REC_SIZE) & 
                ~(LLOG_CHUNK_SIZE - 1);
}


/* sets:
 *  - cur_offset to the furthest point read in the log file
 *  - cur_idx to the log index preceeding cur_offset
 * returns -EIO/-EINVAL on error
 */
static int llog_lvfs_next_block(struct llog_handle *loghandle, int *cur_idx,
                                int next_idx, __u64 *cur_offset, void *buf,
                                int len)
{
        int rc;
        ENTRY;

        if (len == 0 || len & (LLOG_CHUNK_SIZE - 1))
                RETURN(-EINVAL);

        CDEBUG(D_OTHER, "looking for log index %u (cur idx %u off "LPU64"\n",
               next_idx, *cur_idx, *cur_offset);

        while (*cur_offset < loghandle->lgh_file->f_dentry->d_inode->i_size) {
                struct llog_rec_hdr *rec;
                struct llog_rec_tail *tail;

                llog_skip_over(cur_offset, *cur_idx, next_idx);

                rc = lustre_fread(loghandle->lgh_file, buf, LLOG_CHUNK_SIZE, 
                                  cur_offset);

                if (rc == 0) /* end of file, nothing to do */
                        RETURN(0);

                if (rc < sizeof(*tail)) {
                        CERROR("Invalid llog block at log id "LPU64"/%u offset "LPU64"\n",
                               loghandle->lgh_id.lgl_oid, loghandle->lgh_id.lgl_ogen, 
                               *cur_offset);
                         RETURN(-EINVAL);
                }

                tail = buf + rc - sizeof(struct llog_rec_tail);
                *cur_idx = tail->lrt_index;

                /* this shouldn't happen */
                if (tail->lrt_index == 0) {
                        CERROR("Invalid llog tail at log id "LPU64"/%u offset "LPU64"\n",
                               loghandle->lgh_id.lgl_oid, loghandle->lgh_id.lgl_ogen, 
                               *cur_offset);
                        RETURN(-EINVAL);
                }

                /* sanity check that the start of the new buffer is no farther
                 * than the record that we wanted.  This shouldn't happen. */
                rec = buf;
                if (rec->lrh_index > next_idx) {
                        CERROR("missed desired record? %u > %u\n",
                               rec->lrh_index, next_idx);
                        RETURN(-ENOENT);
                }
                RETURN(0);
        }
        RETURN(-EIO);
}

/* This is a callback from the llog_* functions.
 * Assumes caller has already pushed us into the kernel context. */
static int llog_lvfs_create(struct obd_device *obd, struct llog_handle **res,
                            struct llog_logid *logid, char *name)
{
        char logname[24];
        struct llog_handle *handle;
        struct l_dentry *dchild = NULL;
        struct obdo *oa = NULL;
        int rc = 0, cleanup_phase = 1;
        int open_flags = O_RDWR | O_CREAT | O_LARGEFILE;
        ENTRY;

        handle = llog_alloc_handle();
        if (handle == NULL)
                RETURN(-ENOMEM);
        *res = handle;

        if (logid != NULL) {
                dchild = obd_lvfs_fid2dentry(obd->obd_log_exp, logid->lgl_oid,
                                             logid->lgl_ogen, logid->lgl_ogr);

                if (IS_ERR(dchild)) {
                        rc = PTR_ERR(dchild);
                        CERROR("error looking up log file "LPX64":0x%x: rc %d\n",
                               logid->lgl_oid, logid->lgl_ogen, rc);
                        GOTO(cleanup, rc);
                }

                cleanup_phase = 2;
                if (dchild->d_inode == NULL) {
                        rc = -ENOENT;
                        CERROR("nonexistent log file "LPX64":"LPX64": rc %d\n",
                               logid->lgl_oid, logid->lgl_ogr, rc);
                        GOTO(cleanup, rc);
                }

                handle->lgh_file = l_dentry_open(&obd->obd_ctxt, dchild,
                                                    O_RDWR | O_LARGEFILE);
                if (IS_ERR(handle->lgh_file)) {
                        rc = PTR_ERR(handle->lgh_file);
                        CERROR("error opening logfile "LPX64"0x%x: rc %d\n",
                               logid->lgl_oid, logid->lgl_ogen, rc);
                        GOTO(cleanup, rc);
                }
        } else if (name) {
                LASSERT(strlen(name) <= 18);
                sprintf(logname, "LOGS/%s", name);

                handle->lgh_file = l_filp_open(logname, open_flags, 0644);
                if (IS_ERR(handle->lgh_file)) {
                        rc = PTR_ERR(handle->lgh_file);
                        CERROR("logfile creation %s: %d\n", logname, rc);
                        GOTO(cleanup, rc);
                }
        } else {
                oa = obdo_alloc();
                if (oa == NULL) 
                        GOTO(cleanup, rc = -ENOMEM);
                /* XXX get some filter group constants */
                oa->o_gr = 1;
                oa->o_valid = OBD_MD_FLGENER | OBD_MD_FLGROUP;
                rc = obd_create(obd->obd_log_exp, oa, NULL, NULL);
                if (rc)
                        GOTO(cleanup, rc);

                dchild = obd_lvfs_fid2dentry(obd->obd_log_exp, oa->o_id,
                                             oa->o_generation, oa->o_gr);

                if (IS_ERR(dchild))
                        GOTO(cleanup, rc = PTR_ERR(dchild));
                cleanup_phase = 2;
                handle->lgh_file = l_dentry_open(&obd->obd_ctxt, dchild,
                                                 open_flags);
                if (IS_ERR(handle->lgh_file))
                        GOTO(cleanup, rc = PTR_ERR(handle->lgh_file));
        }

        handle->lgh_obd = obd;
        handle->lgh_id.lgl_ogr = 1;
        handle->lgh_id.lgl_oid = handle->lgh_file->f_dentry->d_inode->i_ino;
        handle->lgh_id.lgl_ogen = handle->lgh_file->f_dentry->d_inode->i_generation;
 finish:
        if (oa)
                obdo_free(oa);
        RETURN(rc);
cleanup:
        switch (cleanup_phase) {
        case 2:
                l_dput(dchild);
        case 1:
                llog_free_handle(handle);
        }
        goto finish;
}

static int llog_lvfs_close(struct llog_handle *handle)
{
        int rc;
        ENTRY;

        rc = filp_close(handle->lgh_file, 0);
        if (rc)
                CERROR("error closing log: rc %d\n", rc);
        RETURN(rc);
}

static int llog_lvfs_destroy(struct llog_handle *handle)
{
        struct obdo *oa;
        int rc;
        ENTRY;

        oa = obdo_alloc();
        if (oa == NULL) 
                RETURN(-ENOMEM);

        oa->o_id = handle->lgh_id.lgl_oid;
        oa->o_gr = handle->lgh_id.lgl_ogr;
        oa->o_generation = handle->lgh_id.lgl_ogen;
        oa->o_valid = OBD_MD_FLID | OBD_MD_FLGROUP | OBD_MD_FLGENER;

        rc = llog_lvfs_close(handle);
        if (rc)
                GOTO(out, rc);

        rc = obd_destroy(handle->lgh_obd->obd_log_exp, oa, NULL, NULL);
 out:
        obdo_free(oa);
        RETURN(rc);
}

struct llog_operations llog_lvfs_ops = {
        lop_write_rec:   llog_lvfs_write_rec,
        lop_next_block:  llog_lvfs_next_block,
        lop_read_header: llog_lvfs_read_header,
        lop_create:      llog_lvfs_create,
        lop_destroy:     llog_lvfs_destroy,
        lop_close:       llog_lvfs_close,
        //        lop_cancel: llog_lvfs_cancel,
};

EXPORT_SYMBOL(llog_lvfs_ops);
