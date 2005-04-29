/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2002, Lawrence Livermore National Labs (LLNL)
 * Author: Brian Behlendorf <behlendorf1@llnl.gov>
 *         Kedar Sovani (kedar@calsoftinc.com)
 *         Amey Inamdar (amey@calsoftinc.com)
 *
 * This file is part of Portals, http://www.sf.net/projects/lustre/
 *
 * Portals is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * Portals is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Portals; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define DEBUG_SUBSYSTEM S_PINGER

#include <libcfs/kp30.h>
#include <portals/p30.h>
#include "ping.h"
/* int portal_debug = D_PING_CLI;  */


#define STDSIZE (sizeof(int) + sizeof(int) + sizeof(struct timeval))

#define MAX_TIME 100000

/* This should be enclosed in a structure */

static struct pingcli_data *client = NULL;

static int count = 0;

static void
pingcli_shutdown(ptl_handle_ni_t nih, int err)
{
        int rc;

        /* Yes, we are intentionally allowing us to fall through each
         * case in to the next.  This allows us to pass an error
         * code to just clean up the right stuff.
         */
        switch (err) {
                case 1:
                        /* Unlink any memory descriptors we may have used */
                        if ((rc = PtlMDUnlink (client->md_out_head_h)))
                                PDEBUG ("PtlMDUnlink", rc);
                case 2:
                        if ((rc = PtlMDUnlink (client->md_in_head_h)))
                                PDEBUG ("PtlMDUnlink", rc);

                        /* Free the event queue */
                        if ((rc = PtlEQFree (client->eq)))
                                PDEBUG ("PtlEQFree", rc);

                        if ((rc = PtlMEUnlink (client->me)))
                                PDEBUG ("PtlMEUnlink", rc);
                case 3:
                        PtlNIFini(nih);

                case 4:
                        /* Free our buffers */
                        if (client->outbuf != NULL)
                                PORTAL_FREE (client->outbuf, STDSIZE + client->size);

                        if (client->inbuf != NULL)
                                PORTAL_FREE (client->inbuf,
                                             (client->size + STDSIZE) * client->count);

                        if (client != NULL)
                                PORTAL_FREE (client,
                                                sizeof(struct pingcli_data));
        }


        CDEBUG (D_OTHER, "ping client released resources\n");
} /* pingcli_shutdown() */

static void pingcli_callback(ptl_event_t *ev)
{
        int i;
        unsigned magic;
        i = __le32_to_cpu(*(int *)(ev->md.start + ev->offset + sizeof(unsigned)));
        magic = __le32_to_cpu(*(int *)(ev->md.start + ev->offset));

        if(magic != 0xcafebabe) {
                CERROR("Unexpected response %x\n", magic);
        }

        if((i == count) || !count)
                wake_up_process (client->tsk);
        else
                CERROR("Received response after timeout for %d\n",i);
}


static void
pingcli_start(struct portal_ioctl_data *args)
{
        ptl_handle_ni_t nih = PTL_INVALID_HANDLE;
        unsigned ping_head_magic = __cpu_to_le32(PING_HEADER_MAGIC);
        int rc;
        struct timeval tv1, tv2;
        
        client->tsk = cfs_current();
        client->nid = args->ioc_nid;
        client->count = args->ioc_count;
        client->size = args->ioc_u32[0];
        client->timeout = args->ioc_u32[1];
        
       CDEBUG (D_OTHER, "pingcli_setup args: nid "LPX64" (%s),  \
                        size %u, count: %u, timeout: %u\n",
                        client->nid,
                        libcfs_nid2str(client->nid),
                        client->size, client->count, client->timeout);


        PORTAL_ALLOC (client->outbuf, STDSIZE + client->size) ;
        if (client->outbuf == NULL)
        {
                CERROR ("Unable to allocate out_buf ("LPSZ" bytes)\n", STDSIZE);
                pingcli_shutdown (nih, 4);
                return;
        }

        PORTAL_ALLOC (client->inbuf,
                        (client->size + STDSIZE) * client->count);
        if (client->inbuf == NULL)
        {
                CERROR ("Unable to allocate out_buf ("LPSZ" bytes)\n", STDSIZE);
                pingcli_shutdown (nih, 4);
                return;
        }

        rc = PtlNIInit(PTL_IFACE_DEFAULT, 0, NULL, NULL, &nih);
        if (rc != PTL_OK && rc != PTL_IFACE_DUP)
        {
                CERROR ("PtlNIInit: error %d\n", rc);
                pingcli_shutdown (nih, 4);
                return;
        }

        /* Based on the initialization aquire our unique portal ID. */
        if ((rc = PtlGetId (nih, &client->myid)))
        {
                CERROR ("PtlGetId error %d\n", rc);
                pingcli_shutdown (nih, 2);
                return;
        }

        /* Setup the local match entries */
        client->id_local.nid = PTL_NID_ANY;
        client->id_local.pid = PTL_PID_ANY;

        /* Setup the remote match entries */
        client->id_remote.nid = client->nid;
        client->id_remote.pid = 0;

        if ((rc = PtlMEAttach (nih, PTL_PING_CLIENT,
                   client->id_local, 0, ~0, PTL_RETAIN,
                   PTL_INS_AFTER, &client->me)))
        {
                CERROR ("PtlMEAttach error %d\n", rc);
                pingcli_shutdown (nih, 2);
                return;
        }

        /* Allocate the event queue for this network interface */
        if ((rc = PtlEQAlloc (nih, 64, pingcli_callback, &client->eq)))
        {
                CERROR ("PtlEQAlloc error %d\n", rc);
                pingcli_shutdown (nih, 2);
                return;
        }

        count = client->count;

        client->md_in_head.start     = client->inbuf;
        client->md_in_head.length    = (client->size + STDSIZE) * count;
        client->md_in_head.threshold = PTL_MD_THRESH_INF;
        client->md_in_head.options   = PTL_MD_EVENT_START_DISABLE | PTL_MD_OP_PUT;
        client->md_in_head.user_ptr  = NULL;
        client->md_in_head.eq_handle = client->eq;
        memset (client->inbuf, 0, (client->size + STDSIZE) * count);

        /* Attach the incoming buffer */
        if ((rc = PtlMDAttach (client->me, client->md_in_head,
                              PTL_UNLINK, &client->md_in_head_h))) {
                CERROR ("PtlMDAttach error %d\n", rc);
                pingcli_shutdown (nih, 1);
                return;
        }
        /* Setup the outgoing ping header */
        client->md_out_head.start     = client->outbuf;
        client->md_out_head.length    = STDSIZE + client->size;
        client->md_out_head.threshold = client->count;
        client->md_out_head.options   = PTL_MD_EVENT_START_DISABLE | PTL_MD_OP_PUT;
        client->md_out_head.user_ptr  = NULL;
        client->md_out_head.eq_handle = PTL_EQ_NONE;

        memcpy (client->outbuf, &ping_head_magic, sizeof(ping_head_magic));

        count = 0;

        /* Bind the outgoing ping header */
        if ((rc=PtlMDBind (nih, client->md_out_head,
                           PTL_UNLINK, &client->md_out_head_h))) {
                CERROR ("PtlMDBind error %d\n", rc);
                pingcli_shutdown (nih, 1);
                return;
        }
        while ((client->count - count)) {
                unsigned __count;
                __count = __cpu_to_le32(count);

                memcpy (client->outbuf + sizeof(unsigned),
                       &(__count), sizeof(unsigned));
                 /* Put the ping packet */
                cfs_fs_timeval (&tv1);

                memcpy(client->outbuf+sizeof(unsigned)+sizeof(unsigned),&tv1,
                       sizeof(struct timeval));

                if((rc = PtlPut (client->md_out_head_h, PTL_NOACK_REQ,
                          client->id_remote, PTL_PING_SERVER, 0, 0, 0, 0))) {
                         PDEBUG ("PtlPut (header)", rc);
                         pingcli_shutdown (nih, 1);
                         return;
                }
                CWARN ("Lustre: sent msg no %d.\n", count);

                set_current_state (TASK_INTERRUPTIBLE);
                rc = schedule_timeout (cfs_time_seconds(client->timeout));
                if (rc == 0) {
                        CERROR ("timeout .....\n");
                } else {
                        cfs_fs_timeval (&tv2);
                        CWARN("Reply in %u usec\n",
                              (unsigned)((tv2.tv_sec - tv1.tv_sec)
                                         * 1000000 +  (tv2.tv_usec - tv1.tv_usec)));
                }
                count++;
        }

        pingcli_shutdown (nih, 2);

} /* pingcli_setup() */



/* called by the portals_ioctl for ping requests */
int kping_client(struct portal_ioctl_data *args)
{
        PORTAL_ALLOC (client, sizeof(struct pingcli_data));
        if (client == NULL)
        {
                CERROR ("Unable to allocate client structure\n");
                return (0);
        }
        memset (client, 0, sizeof(struct pingcli_data));
        pingcli_start (args);

        return 0;
} /* kping_client() */


static int __init pingcli_init(void)
{
        PORTAL_SYMBOL_REGISTER(kping_client);
        return 0;
} /* pingcli_init() */


static void /*__exit*/ pingcli_cleanup(void)
{
        PORTAL_SYMBOL_UNREGISTER (kping_client);
} /* pingcli_cleanup() */


MODULE_AUTHOR("Brian Behlendorf (LLNL)");
MODULE_DESCRIPTION("A simple kernel space ping client for portals testing");
MODULE_LICENSE("GPL");

cfs_module(ping_cli, "1.0.0", pingcli_init, pingcli_cleanup);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0))
EXPORT_SYMBOL (kping_client);
#endif
