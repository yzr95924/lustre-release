/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * p30/lib-types.h
 *
 * Types used by the library side routines that do not need to be
 * exposed to the user application
 */

#ifndef __PORTALS_LIB_TYPES_H__
#define __PORTALS_LIB_TYPES_H__

#include "build_check.h"

#if defined(__linux__)
#include <portals/linux/lib-types.h>
#elif defined(__APPLE__)
#include <portals/darwin/lib-types.h>
#else
#error Unsupported Operating System
#endif

#include <libcfs/libcfs.h>
#include <libcfs/list.h>
#include <portals/types.h>

#define WIRE_ATTR	__attribute__((packed))

/* The wire handle's interface cookie only matches one network interface in
 * one epoch (i.e. new cookie when the interface restarts or the node
 * reboots).  The object cookie only matches one object on that interface
 * during that object's lifetime (i.e. no cookie re-use). */
typedef struct {
        __u64 wh_interface_cookie;
        __u64 wh_object_cookie;
} WIRE_ATTR ptl_handle_wire_t;

/* byte-flip insensitive! */
#define PTL_WIRE_HANDLE_NONE \
((const ptl_handle_wire_t) {.wh_interface_cookie = -1, .wh_object_cookie = -1})

typedef enum {
        PTL_MSG_ACK = 0,
        PTL_MSG_PUT,
        PTL_MSG_GET,
        PTL_MSG_REPLY,
        PTL_MSG_HELLO,
} ptl_msg_type_t;

/* The variant fields of the portals message header are aligned on an 8
 * byte boundary in the message header.  Note that all types used in these
 * wire structs MUST be fixed size and the smaller types are placed at the
 * end. */
typedef struct ptl_ack {
        ptl_handle_wire_t  dst_wmd;
        ptl_match_bits_t   match_bits;
        ptl_size_t         mlength;
} WIRE_ATTR ptl_ack_t;

typedef struct ptl_put {
        ptl_handle_wire_t  ack_wmd;
        ptl_match_bits_t   match_bits;
        ptl_hdr_data_t     hdr_data;
        ptl_pt_index_t     ptl_index;
        ptl_size_t         offset;
} WIRE_ATTR ptl_put_t;

typedef struct ptl_get {
        ptl_handle_wire_t  return_wmd;
        ptl_match_bits_t   match_bits;
        ptl_pt_index_t     ptl_index;
        ptl_size_t         src_offset;
        ptl_size_t         sink_length;
} WIRE_ATTR ptl_get_t;

typedef struct ptl_reply {
        ptl_handle_wire_t  dst_wmd;
} WIRE_ATTR ptl_reply_t;

typedef struct ptl_hello {
        __u64              incarnation;
        __u32              type;
} WIRE_ATTR ptl_hello_t;

typedef struct {
        ptl_nid_t           dest_nid;
        ptl_nid_t           src_nid;
        ptl_pid_t           dest_pid;
        ptl_pid_t           src_pid;
        __u32               type;               /* ptl_msg_type_t */
        __u32               payload_length;     /* payload data to follow */
        /*<------__u64 aligned------->*/
        union {
                ptl_ack_t   ack;
                ptl_put_t   put;
                ptl_get_t   get;
                ptl_reply_t reply;
                ptl_hello_t hello;
        } msg;
} WIRE_ATTR ptl_hdr_t;

/* A HELLO message contains the portals magic number and protocol version
 * code in the header's dest_nid, the peer's NID in the src_nid, and
 * PTL_MSG_HELLO in the type field.  All other common fields are zero
 * (including payload_size; i.e. no payload).  
 * This is for use by byte-stream NALs (e.g. TCP/IP) to check the peer is
 * running the same protocol and to find out its NID, so that hosts with
 * multiple IP interfaces can have a single NID. These NALs should exchange
 * HELLO messages when a connection is first established. 
 * Individual NALs can put whatever else they fancy in ptl_hdr_t::msg. 
 */
typedef struct {
        __u32	magic;                          /* PORTALS_PROTO_MAGIC */
        __u16   version_major;                  /* increment on incompatible change */
        __u16   version_minor;                  /* increment on compatible change */
} WIRE_ATTR ptl_magicversion_t;

#define PORTALS_PROTO_MAGIC                0xeebc0ded

#define PORTALS_PROTO_VERSION_MAJOR        1
#define PORTALS_PROTO_VERSION_MINOR        0

/* limit on the number of entries in discontiguous MDs */
#define PTL_MTU        (1<<20)
#define PTL_MD_MAX_IOV 256

/* forward refs */
struct ptl_libmd;

typedef struct ptl_msg {
        struct list_head   msg_list;
        struct ptl_libmd  *msg_md;
        ptl_handle_wire_t  msg_ack_wmd;
        ptl_event_t        msg_ev;
} ptl_msg_t;

typedef struct ptl_libhandle {
        struct list_head  lh_hash_chain;
        __u64             lh_cookie;
} ptl_libhandle_t;

#define lh_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

typedef struct ptl_eq {
        struct list_head  eq_list;
        ptl_libhandle_t   eq_lh;
        ptl_seq_t         eq_enq_seq;
        ptl_seq_t         eq_deq_seq;
        ptl_size_t        eq_size;
        ptl_event_t      *eq_events;
        int               eq_refcount;
        ptl_eq_handler_t  eq_callback;
} ptl_eq_t;

typedef struct ptl_me {
        struct list_head  me_list;
        ptl_libhandle_t   me_lh;
        ptl_process_id_t  me_match_id;
        ptl_match_bits_t  me_match_bits;
        ptl_match_bits_t  me_ignore_bits;
        ptl_unlink_t      me_unlink;
        struct ptl_libmd *me_md;
} ptl_me_t;

typedef struct ptl_libmd {
        struct list_head  md_list;
        ptl_libhandle_t   md_lh;
        ptl_me_t         *md_me;
        char             *md_start;
        ptl_size_t        md_offset;
        ptl_size_t        md_length;
        ptl_size_t        md_max_size;
        int               md_threshold;
        int               md_pending;
        unsigned int      md_options;
        unsigned int      md_flags;
        void             *md_user_ptr;
        ptl_eq_t         *md_eq;
        void             *md_addrkey;
        unsigned int      md_niov;                /* # frags */
        union {
                struct iovec  iov[PTL_MD_MAX_IOV];
                ptl_kiov_t    kiov[PTL_MD_MAX_IOV];
        } md_iov;
} ptl_libmd_t;

#define PTL_MD_FLAG_ZOMBIE            (1 << 0)
#define PTL_MD_FLAG_AUTO_UNLINK       (1 << 1)

#ifdef PTL_USE_LIB_FREELIST
typedef struct
{
        void	          *fl_objs;             /* single contiguous array of objects */
        int                fl_nobjs;            /* the number of them */
        int                fl_objsize;          /* the size (including overhead) of each of them */
        struct list_head   fl_list;             /* where they are enqueued */
} ptl_freelist_t;

typedef struct
{
        struct list_head   fo_list;             /* enqueue on fl_list */
        void              *fo_contents;         /* aligned contents */
} ptl_freeobj_t;
#endif

typedef struct {
        /* info about peers we are trying to fail */
        struct list_head  tp_list;             /* apini_test_peers */
        ptl_nid_t         tp_nid;              /* matching nid */
        unsigned int      tp_threshold;        /* # failures to simulate */
} ptl_test_peer_t;

#define PTL_COOKIE_TYPE_MD    1
#define PTL_COOKIE_TYPE_ME    2
#define PTL_COOKIE_TYPE_EQ    3
#define PTL_COOKIE_TYPES      4
/* PTL_COOKIE_TYPES must be a power of 2, so the cookie type can be
 * extracted by masking with (PTL_COOKIE_TYPES - 1) */

struct ptl_ni;                                  /* forward ref */

/******************************************************************************/
/* Portals Router */

typedef void (*kpr_fwd_callback_t)(struct ptl_ni *ni,
                                   void *arg, int error);

/* space for routing targets to stash "stuff" in a forwarded packet */
typedef union {
        long long        _alignment;
        void            *_space[16];            /* scale with CPU arch */
} kprfd_scratch_t;

/* Kernel Portals Routing Forwarded message Descriptor */
typedef struct {
        struct list_head     kprfd_list;        /* stash in queues (routing target can use) */
        ptl_nid_t            kprfd_target_nid;  /* final destination NID */
        ptl_nid_t            kprfd_gateway_nid; /* gateway NID */
        ptl_hdr_t           *kprfd_hdr;         /* header in wire byte order */
        int                  kprfd_nob;         /* # payload bytes */
        int                  kprfd_niov;        /* # payload frags */
        ptl_kiov_t          *kprfd_kiov;        /* payload fragments */
        struct ptl_ni       *kprfd_src_ni;      /* originating NI */
        kpr_fwd_callback_t   kprfd_callback;    /* completion callback */
        void                *kprfd_callback_arg; /* completion callback arg */
        kprfd_scratch_t      kprfd_scratch;     /* scratchpad for routing targets */
} kpr_fwd_desc_t;

/******************************************************************************/

typedef struct ptl_nal
{
        /* fields managed by portals */
        struct list_head  nal_list;             /* stash in the NAL table */
        atomic_t          nal_refcount;         /* # active instances */

        /* fields initialised by the NAL */
        unsigned int      nal_type;
        
        ptl_err_t  (*nal_startup) (struct ptl_ni *ni, char **interfaces);
        void       (*nal_shutdown) (struct ptl_ni *ni);

        int        (*nal_ctl)(struct ptl_ni *ni, unsigned int cmd, void *arg);
        
	/*
	 * send: Sends a preformatted header and payload data to a
	 * specified remote process. The payload is scattered over 'niov'
	 * fragments described by iov, starting at 'offset' for 'mlen'
	 * bytes.  
	 * NB the NAL may NOT overwrite iov.  
	 * PTL_OK on success => NAL has committed to send and will call
	 * ptl_finalize on completion
	 */
	ptl_err_t (*nal_send) 
                (struct ptl_ni *ni, void *private, ptl_msg_t *msg, 
                 ptl_hdr_t *hdr, int type, ptl_process_id_t target,
                 int routing, unsigned int niov, struct iovec *iov, 
                 size_t offset, size_t mlen);
        
	/* as send, but with a set of page fragments (NULL if not supported) */
	ptl_err_t (*nal_send_pages)
                (struct ptl_ni *ni, void *private, ptl_msg_t *cookie, 
                 ptl_hdr_t *hdr, int type, ptl_process_id_t target, 
                 int routing, unsigned int niov, ptl_kiov_t *iov, 
                 size_t offset, size_t mlen);
	/*
	 * recv: Receives an incoming message from a remote process.  The
	 * payload is to be received into the scattered buffer of 'niov'
	 * fragments described by iov, starting at 'offset' for 'mlen'
	 * bytes.  Payload bytes after 'mlen' up to 'rlen' are to be
	 * discarded.  
	 * NB the NAL may NOT overwrite iov.
	 * PTL_OK on success => NAL has committed to receive and will call
	 * ptl_finalize on completion
	 */
	ptl_err_t (*nal_recv) 
                (struct ptl_ni *ni, void *private, ptl_msg_t * cookie,
                 unsigned int niov, struct iovec *iov, 
                 size_t offset, size_t mlen, size_t rlen);

	/* as recv, but with a set of page fragments (NULL if not supported) */
	ptl_err_t (*nal_recv_pages) 
                (struct ptl_ni *ni, void *private, ptl_msg_t * cookie,
                 unsigned int niov, ptl_kiov_t *iov, 
                 size_t offset, size_t mlen, size_t rlen);

        /* forward a packet for the router */
        void (*nal_fwd)(struct ptl_ni *ni, kpr_fwd_desc_t *fwd);        

        /* notification of peer health */
        void (*nal_notify)(struct ptl_ni *ni, ptl_nid_t peer, int alive);
        
} ptl_nal_t;

typedef struct ptl_ni {
        struct list_head  ni_list;              /* chain on apini_nis */
        ptl_nid_t         ni_nid;               /* interface's NID */
        void             *ni_data;              /* instance-specific data */
        ptl_nal_t        *ni_nal;               /* procedural interface */
        int               ni_shutdown;          /* shutting down? */
        atomic_t          ni_refcount;          /* reference count */
} ptl_ni_t;

typedef struct                                  /* loopback descriptor */
{
        unsigned int     lod_type;
        unsigned int     lod_niov;
        size_t           lod_offset;
        size_t           lod_nob;
        union {
                struct iovec  *iov;
                ptl_kiov_t    *kiov;
        }                lod_iov;
} lo_desc_t;

#define LOD_IOV     0xeb105
#define LOD_KIOV    0xeb106

typedef struct
{
        int               apini_refcount;       /* PtlNIInit/PtlNIFini counter */

        int               apini_nportals;       /* # portals */
        struct list_head *apini_portals;        /* the vector of portals */

        ptl_pid_t         apini_pid;            /* requested pid */
        ptl_ni_limits_t   apini_actual_limits;

        struct list_head  apini_nis;            /* NAL instances */
        struct list_head  apini_zombie_nis;     /* dying NAL instances */
        int               apini_nzombie_nis;    /* # of NIS to wait for */

        int               apini_lh_hash_size;   /* size of lib handle hash table */
        struct list_head *apini_lh_hash_table;  /* all extant lib handles, this interface */
        __u64             apini_next_object_cookie; /* cookie generator */
        __u64             apini_interface_cookie; /* uniquely identifies this ni in this epoch */
        
        struct list_head  apini_test_peers;
        
#ifdef PTL_USE_LIB_FREELIST
        ptl_freelist_t    apini_free_mes;
        ptl_freelist_t    apini_free_msgs;
        ptl_freelist_t    apini_free_mds;
        ptl_freelist_t    apini_free_eqs;
#endif

        struct list_head  apini_active_msgs;
        struct list_head  apini_active_mds;
        struct list_head  apini_active_eqs;

#ifdef __KERNEL__
        spinlock_t        apini_lock;
        cfs_waitq_t       apini_waitq;
#else
        pthread_mutex_t   apini_mutex;
        pthread_cond_t    apini_cond;
#endif

        struct {
                long       recv_count;
                long       recv_length;
                long       send_count;
                long       send_length;
                long       drop_count;
                long       drop_length;
                long       msgs_alloc;
                long       msgs_max;
        }                 apini_counters;
        
} ptl_apini_t;

#endif
