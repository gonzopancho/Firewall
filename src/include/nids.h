/*
  Copyright (c) 1999 Rafal Wojtczuk <nergal@7bulls.com>. All rights reserved.
  See the file COPYING for license details.
*/

#ifndef _NIDS_NIDS_H
# define _NIDS_NIDS_H

#include <sys/types.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

# ifdef __cplusplus
extern "C" {
# endif

# define NIDS_MAJOR 1
# define NIDS_MINOR 24

enum
{
  NIDS_WARN_IP = 1,
  NIDS_WARN_TCP,
  NIDS_WARN_UDP,
  NIDS_WARN_SCAN
};

enum
{
  NIDS_WARN_UNDEFINED = 0,
  NIDS_WARN_IP_OVERSIZED,
  NIDS_WARN_IP_INVLIST,
  NIDS_WARN_IP_OVERLAP,
  NIDS_WARN_IP_HDR,
  NIDS_WARN_IP_SRR,
  NIDS_WARN_TCP_TOOMUCH,
  NIDS_WARN_TCP_HDR,
  NIDS_WARN_TCP_BIGQUEUE,
  NIDS_WARN_TCP_BADFLAGS
};

# define NIDS_JUST_EST 1
# define NIDS_DATA 2
# define NIDS_CLOSE 3
# define NIDS_RESET 4
# define NIDS_TIMED_OUT 5
# define NIDS_EXITING   6	/* nids is exiting; last chance to get data */

# define NIDS_DO_CHKSUM  0
# define NIDS_DONT_CHKSUM 1

struct tuple4
{
  uint16_t source;
  uint16_t dest;
  uint32_t saddr;
  uint32_t daddr;
};

struct half_stream
{
  char state;
  u_char ts_on;
  u_char wscale_on;
  u_char urg_seen;

  u_short window;

  struct skbuff *list;
  struct skbuff *listtail;

  int rmem_alloc;
  int urg_count;
//  u_int acked;
  u_int seq;
  u_int ack_seq;
  u_int first_data_seq;
//  u_char urgdata;
  u_int urg_ptr;
  u_int curr_ts; 
  u_int wscale;

// Not usually used
#if !defined(DISABLE_UPPER_LAYER)
  char collect;
  char collect_urg;
  u_char count_new_urg;
  int offset;
  int bufsize;
#endif
  int count;
  int count_new;
  char *data;
};

struct tcp_stream
{
  struct tuple4 addr;
  char nids_state;
  struct half_stream client;
  struct half_stream server;
  struct lurker_node *listeners;
  int hash_index;
  int read;
#if defined(ORIGIN_TCP)
  struct tcp_stream *next_free;
  struct tcp_stream *next_node;
  struct tcp_stream *prev_node;
  struct tcp_stream *next_time;
  struct tcp_stream *prev_time;
  void *user;
#endif
};

struct nids_prm
{
  int n_tcp_streams;
  int n_hosts;
  char *device;
  char *filename;
  int sk_buff_size;
  int dev_addon;
  int scan_num_hosts;
  int scan_delay;
  int scan_num_ports;
  void (*no_mem) (char *);
  int (*ip_filter) ();
  char *pcap_filter;
  int promisc;
  int one_loop_less;
  int pcap_timeout;
  int multiproc;
  int queue_limit;
  int tcp_workarounds;
};

struct tcp_timeout
{
  struct tcp_stream *a_tcp;
  struct timeval timeout;
  struct tcp_timeout *next;
  struct tcp_timeout *prev;
};

int nids_init (int);
void nids_killtcp (struct tcp_stream *);
void nids_discard (struct tcp_stream *, int);
int nids_run (void);
void nids_exit(void);
int nids_getfd (void);
int nids_dispatch (int);
int nids_next (void);
//struct tcp_stream *nids_find_tcp_stream(struct tuple4 *);
//void nids_free_tcp_stream(struct tcp_stream *);

extern struct nids_prm nids_params;
extern char *nids_warnings[];
extern char nids_errbuf[];
extern u_int nids_linkoffset;
extern struct tcp_timeout *nids_tcp_timeouts;

struct nids_chksum_ctl {
	u_int netaddr;
	u_int mask;
	u_int action;
	u_int reserved;
};
extern void nids_register_chksum_ctl(struct nids_chksum_ctl *, int);

# ifdef __cplusplus
}
# endif

#endif /* _NIDS_NIDS_H */
