#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#if defined(CRC_SIGN) || defined(CRC_SIGN1) || defined(CRC_SIGN2)
#include <nmmintrin.h>
#endif

#include "nids.h"
#include "util.h"
#include "bitmap.h"
#include "dhash_tcp.h"
#include "tcp.h"
#include "parallel.h"
#include "mem.h"
#include "fire_config.h"

//#define DEBUG

#if defined(MAJOR_INDEXFREE_TCP)

#if 0
#define SET_NUMBER 200000 
#define TOTAL_CACHE_ELEM_NUM 3200000
#define CACHE_ELEM_NUM (3200000 / (config->worker_num - 1)) // element number stored in cache
#else
#define SET_NUMBER 100000 //0.1 Million buckets = 1.6 Million Elem
#define TOTAL_CACHE_ELEM_NUM 1600000
#define CACHE_ELEM_NUM (1600000 / (config->worker_num - 1)) // element number stored in cache, 100000 * 16
#endif

extern TEST_SET tcp_test[MAX_CPU_CORES];
extern fire_config_t *config;

extern struct proc_node *tcp_procs;
extern pthread_key_t tcp_context;

static int cache_elem_num;
static int core_elem_num;

extern int get_ts(struct tcphdr *, unsigned int *);
extern int get_wscale(struct tcphdr *, unsigned int *);
extern void del_tcp_closing_timeout(struct tcp_stream *);
extern void purge_queue(struct half_stream *);
extern void handle_ack(struct half_stream *, u_int);
extern void tcp_queue(struct tcp_stream *, struct tcphdr *,
	struct half_stream *, struct half_stream *,
	char *, int, int);
extern void prune_queue(struct half_stream *, struct tcphdr *);

int is_false_positive(struct tuple4 addr, idx_type tcb_index)
{
	tcp_context_t *tcp_thread_local_p = pthread_getspecific(tcp_context);

	struct tcp_stream *tcb_p = &(tcp_thread_local_p->tcb_array[tcb_index]);
	if (!((addr.source == tcb_p->addr.source &&
		addr.dest == tcb_p->addr.dest &&
		addr.saddr == tcb_p->addr.saddr &&
		addr.daddr == tcb_p->addr.daddr ) ||
		(addr.dest == tcb_p->addr.source &&
		addr.source == tcb_p->addr.dest &&
		addr.daddr == tcb_p->addr.saddr &&
		addr.saddr == tcb_p->addr.daddr ))) {

		// Yes, it is false positive
#if defined(DEBUG)
		tcp_test[tcp_thread_local_p->self_cpu_id].false_positive ++;
#endif

#if 0		
		int sign2 = calc_signature(
				tcb_p->addr.saddr,
				tcb_p->addr.daddr,
				tcb_p->addr.source,
				tcb_p->addr.dest);
		printf("||the Founded one in the table: Sip: %d.%d.%d.%d, Sport:%d, Dip : %d.%d.%d.%d, Dport:%d , sign = %x\n", 
				tcb_p->addr.saddr & 0x000000FF,
				(tcb_p->addr.saddr & 0x0000FF00)>>8,
				(tcb_p->addr.saddr & 0x00FF0000)>>16,
				(tcb_p->addr.saddr & 0xFF000000)>>24,
				tcb_p->addr.source,
				tcb_p->addr.daddr & 0x000000FF,
				(tcb_p->addr.daddr & 0x0000FF00)>>8,
				(tcb_p->addr.daddr & 0x00FF0000)>>16,
				(tcb_p->addr.daddr & 0xFF000000)>>24,
				tcb_p->addr.dest,
				sign2
		      );
		int crc1 = 0;
		crc1 = _mm_crc32_u32(crc1, tcb_p->addr.saddr);
		crc1 = _mm_crc32_u32(crc1, tcb_p->addr.daddr);
		crc1 = _mm_crc32_u32(crc1, tcb_p->addr.source ^ tcb_p->addr.dest);
		printf("(%x", crc1);
		crc1 = 0;
		crc1 = _mm_crc32_u32(crc1, tcb_p->addr.daddr);
		crc1 = _mm_crc32_u32(crc1, tcb_p->addr.saddr);
		crc1 = _mm_crc32_u32(crc1, tcb_p->addr.source ^ tcb_p->addr.dest);
		printf("--  %x)\n", crc1);
		sign2 = calc_signature(
				addr.saddr,
				addr.daddr,
				addr.source,
				addr.dest);
		printf("Current one: Sip: %d.%d.%d.%d, Sport:%d, Dip : %d.%d.%d.%d, Dport:%d , sign = %x||\n", 
				addr.saddr & 0x000000FF,
				(addr.saddr & 0x0000FF00)>>8,
				(addr.saddr & 0x00FF0000)>>16,
				(addr.saddr & 0xFF000000)>>24,
				addr.source,
				addr.daddr & 0x000000FF,
				(addr.daddr & 0x0000FF00)>>8,
				(addr.daddr & 0x00FF0000)>>16,
				(addr.daddr & 0xFF000000)>>24,
				addr.dest,
				sign2
		      );
		crc1 = 0;
		crc1 = _mm_crc32_u32(crc1, addr.saddr);
		crc1 = _mm_crc32_u32(crc1, addr.daddr);
		crc1 = _mm_crc32_u32(crc1, addr.source ^ addr.dest);
		printf("(%x", crc1);
		crc1 = 0;
		crc1 = _mm_crc32_u32(crc1, addr.daddr);
		crc1 = _mm_crc32_u32(crc1, addr.saddr);
		crc1 = _mm_crc32_u32(crc1, addr.source ^ addr.dest);
		printf("--  %x)\n", crc1);
#endif

		return 1;
	} else {
		return 0;
	}
}

u_int
mk_hash_index(struct tuple4 addr)
{
	tcp_context_t *tcp_thread_local_p = pthread_getspecific(tcp_context);

#if defined(CRC_HASH)
	unsigned int crc1 = 0;
	uint32_t port = addr.source ^ addr.dest;
	crc1 = _mm_crc32_u32(crc1, addr.saddr ^ addr.daddr);
	crc1 = _mm_crc32_u32(crc1, port);
	return crc1 % tcp_thread_local_p->tcp_stream_table_size;
#else
	u_int hash = addr.saddr ^ addr.source ^ addr.daddr ^ addr.dest;
	return hash % tcp_thread_local_p->tcp_stream_table_size;
#endif
}

// This can be altered to better algorithm, 
// four bits for indexing 16 way-associative
static inline uint8_t 
get_major_location(sig_type sign)
{
	// the least significant 4 bits
	return (sign & 0x0f) ^ ((sign & 0x0f0000) >> 16);
}

// Here the 16 set-associative array is divided into 4 subsets
// Use the 3rd and 4th bits as the subset index
static inline uint8_t
get_subset_index(sig_type sign)
{
	return sign & 0x0c;
}

struct tcp_stream *
find_stream(struct tcphdr *this_tcphdr, struct ip *this_iphdr, int *from_client)
{
	tcp_context_t *tcp_thread_local_p = pthread_getspecific(tcp_context);

	int hash_index, i;
	elem_type *ptr;
	elem_list_type *ptr_l;
	sig_type sign;
	struct tuple4 addr;
	idx_type tcb_index;
	
	addr.source = this_tcphdr->th_sport;
	addr.dest = this_tcphdr->th_dport;
	addr.saddr = this_iphdr->ip_src.s_addr;
	addr.daddr = this_iphdr->ip_dst.s_addr;

	hash_index = mk_hash_index(addr);

	sign = calc_signature(this_iphdr->ip_src.s_addr,
			this_iphdr->ip_dst.s_addr,
			this_tcphdr->th_sport,
			this_tcphdr->th_dport);

	// Search the cache
	elem_type *set_header = (elem_type *)&(((char *)tcp_thread_local_p->tcp_stream_table)[hash_index * SET_SIZE]);

#if defined(DEBUG)
	tcp_test[tcp_thread_local_p->self_cpu_id].search_num ++;
#endif

#if defined(MAJOR_LOCATION)
	uint8_t loc = get_major_location(sign);

#if defined(DEBUG)
	tcp_test[tcp_thread_local_p->self_cpu_id].step ++;
#endif

	// Search the Major location first
	if (sig_match_e(sign, set_header + loc)) {
		tcb_index = calc_index(hash_index, loc);

		// False positive test
		if (!is_false_positive(addr, tcb_index)) {
			if (addr.source == tcp_thread_local_p->tcb_array[tcb_index].addr.source)
				*from_client = 1;
			else
				*from_client = 0;

#if defined(DEBUG)
			tcp_test[tcp_thread_local_p->self_cpu_id].search_hit_num ++;
#endif
			return &(tcp_thread_local_p->tcb_array)[tcb_index];
		}
	}

	uint8_t major = loc;
	// From start to previous subset
	for (loc = major; loc < SET_ASSOCIATIVE; loc ++) {
#if defined(DEBUG)
		tcp_test[tcp_thread_local_p->self_cpu_id].step ++;
#endif
		if (sig_match_e(sign, set_header + loc)) {
			tcb_index = calc_index(hash_index, loc);

			// False positive test
			if (is_false_positive(addr, tcb_index)) continue;

			if (addr.source == tcp_thread_local_p->tcb_array[tcb_index].addr.source)
				*from_client = 1;
			else
				*from_client = 0;

			return &(tcp_thread_local_p->tcb_array)[tcb_index];
		}
	}
	for (loc = 0; loc < major; loc ++) {
#if defined(DEBUG)
		tcp_test[tcp_thread_local_p->self_cpu_id].step ++;
#endif
		if (sig_match_e(sign, set_header + loc)) {
			tcb_index = calc_index(hash_index, loc);

			// False positive test
			if (is_false_positive(addr, tcb_index)) continue;

			if (addr.source == tcp_thread_local_p->tcb_array[tcb_index].addr.source)
				*from_client = 1;
			else
				*from_client = 0;

			return &(tcp_thread_local_p->tcb_array)[tcb_index];
		}
	}
#else

	for (ptr = set_header, i = 0;
		i < SET_ASSOCIATIVE;
		i ++, ptr ++) {
		
#if defined(DEBUG)
		tcp_test[tcp_thread_local_p->self_cpu_id].step ++;
#endif
		
		if (sig_match_e(sign, ptr)) {
			tcb_index = calc_index(hash_index, i);

			// False positive test
			if (is_false_positive(addr, tcb_index)) continue;

			if (addr.source == tcp_thread_local_p->tcb_array[tcb_index].addr.source)
				*from_client = 1;
			else
				*from_client = 0;

			return &(tcp_thread_local_p->tcb_array)[tcb_index];
		}
	}
#endif

	// Not in cache, search collision linked list
	for (ptr_l = tcp_thread_local_p->conflict_list[hash_index];
		ptr_l != NULL;
		ptr_l = ptr_l->next) {
#if defined(DEBUG)
		tcp_test[tcp_thread_local_p->self_cpu_id].step ++;
#endif
		
		if (sig_match_l(sign, ptr_l)) {

			// False positive test
			if (is_false_positive(addr, index_l(ptr_l))) continue;

			if (addr.source == tcp_thread_local_p->tcb_array[index_l(ptr_l)].addr.source)
				*from_client = 1;
			else
				*from_client = 0;

			return &(tcp_thread_local_p->tcb_array)[index_l(ptr_l)];
		}
	}

	// Not found
#if defined(DEBUG)
	tcp_test[tcp_thread_local_p->self_cpu_id].not_found ++;
#endif
	return NULL;
}

static idx_type add_into_cache(struct tuple4 addr)
{
	tcp_context_t *tcp_thread_local_p = pthread_getspecific(tcp_context);

	sig_type sign;
	int hash_index, i;
	elem_type *ptr;
	elem_list_type *ptr_l, **head_l;
	idx_type tcb_index;

	sign = calc_signature(addr.saddr, addr.daddr, addr.source, addr.dest);

	hash_index = mk_hash_index(addr);

	// Search the cache
	elem_type *set_header = (elem_type *)&(((char *)tcp_thread_local_p->tcp_stream_table)[hash_index * SET_SIZE]);

#if defined(MAJOR_LOCATION)
	uint8_t loc = get_major_location(sign);

#if defined(DEBUG)
	tcp_test[tcp_thread_local_p->self_cpu_id].add_num ++;
#endif
	if (sig_match_e(0, set_header + loc)) {
		ptr = set_header + loc;
		ptr->signature = sign;
#if defined(DEBUG)
		tcp_test[tcp_thread_local_p->self_cpu_id].add_hit_num ++;
#endif
		return calc_index(hash_index, loc);
	}

	uint8_t major = loc;
	// From start to previous subset
	for (loc = major; loc < SET_ASSOCIATIVE; loc ++) {
		if (sig_match_e(0, set_header + loc)) {
			ptr = set_header + loc;
			ptr->signature = sign;
			return calc_index(hash_index, loc);
		}
	}

	for (loc = 0; loc < major; loc ++) {
		if (sig_match_e(0, set_header + loc)) {
			ptr = set_header + loc;
			ptr->signature = sign;
			return calc_index(hash_index, loc);
		}
	}
#else
	for (ptr = set_header, i = 0;
		i < SET_ASSOCIATIVE;
		i ++, ptr ++) {
		
		if (sig_match_e(0, ptr)) {
			ptr->signature = sign;
			return calc_index(hash_index, i);
		}
	}
#endif

#if defined(DEBUG)
	tcp_test[tcp_thread_local_p->self_cpu_id].conflict_into_list ++;
#endif

	// Insert into the collision list
#if defined(MEM_LL)
	ptr_l = mem_alloc(SIZE_LIST_ELEM, tcp_thread_local_p->self_cpu_id);
#else
	ptr_l = (elem_list_type *)malloc(sizeof(elem_list_type));
#endif

	// get free index from bitmap
	// Store the TCB in collision linked list in the part above CACHE_ELEM_NUM
	// in TCB array.
	tcb_index = bitmap_get_free_index(tcp_thread_local_p) + cache_elem_num;
	store_index_l(tcb_index, ptr_l);
	store_sig_l(sign, ptr_l);
	head_l = &(tcp_thread_local_p->conflict_list[hash_index]);

	ptr_l->next = *head_l;
	*head_l = ptr_l;
	return tcb_index;
}

void
add_new_tcp(struct tcphdr *this_tcphdr, struct ip *this_iphdr)
{
	tcp_context_t *tcp_thread_local_p = pthread_getspecific(tcp_context);

	struct tcp_stream *tolink;
	struct tcp_stream *a_tcp;
	struct tuple4 addr;
	idx_type index;
	int core;

	addr.source = this_tcphdr->th_sport;
	addr.dest = this_tcphdr->th_dport;
	addr.saddr = this_iphdr->ip_src.s_addr;
	addr.daddr = this_iphdr->ip_dst.s_addr;

	core = tcp_thread_local_p->self_cpu_id;
#if defined(DEBUG)
	tcp_test[core].tcp_num ++;
	tcp_test[core].total_tcp_num ++;
	if (tcp_test[core].tcp_num > tcp_test[core].max_tcp_num) {
		tcp_test[core].max_tcp_num = tcp_test[core].tcp_num;
	}
#endif

	// add the index into hash cache
	index = add_into_cache(addr);
	if (index >= core_elem_num) {
		printf("Too many conflict into list, index = %d, conflict into list = %d\n", index, tcp_test[tcp_thread_local_p->self_cpu_id].conflict_into_list);
		exit(0);
	}

	// let's have the block
	a_tcp = &(tcp_thread_local_p->tcb_array[index]);

	// fill the tcp block
	memset(a_tcp, 0, sizeof(struct tcp_stream));
	a_tcp->addr = addr;
	a_tcp->client.state = TCP_SYN_SENT;
	a_tcp->client.seq = ntohl(this_tcphdr->th_seq) + 1;
	a_tcp->client.first_data_seq = a_tcp->client.seq;
	a_tcp->client.window = ntohs(this_tcphdr->th_win);
	a_tcp->client.ts_on = get_ts(this_tcphdr, &a_tcp->client.curr_ts);
	a_tcp->client.wscale_on = get_wscale(this_tcphdr, &a_tcp->client.wscale);
	a_tcp->server.state = TCP_CLOSE;

	return;
}

static idx_type 
delete_from_cache(struct tcp_stream *a_tcp)
{
	tcp_context_t *tcp_thread_local_p = pthread_getspecific(tcp_context);

	sig_type sign;
	idx_type tcb_index; 
	int hash_index, i;
	elem_type *ptr;
	elem_list_type *ptr_l, *pre_l;
	struct tuple4 addr;

	addr = a_tcp->addr;
	sign = calc_signature(addr.saddr, addr.daddr, addr.source, addr.dest);

	hash_index = mk_hash_index(addr);

	// Search the cache
	elem_type *set_header = (elem_type *)&(((char *)tcp_thread_local_p->tcp_stream_table)[hash_index * SET_SIZE]);

#if defined(MAJOR_LOCATION)
	uint8_t loc = get_major_location(sign);
	
#if defined(DEBUG)
	tcp_test[tcp_thread_local_p->self_cpu_id].delete_num ++;
#endif
	if (sig_match_e(sign, set_header + loc)) {
		tcb_index = calc_index(hash_index, loc);

		// False positive test
		if (!is_false_positive(addr, tcb_index)) {
			ptr = set_header + loc;
			ptr->signature = 0;
#if defined(DEBUG)
			tcp_test[tcp_thread_local_p->self_cpu_id].delete_hit_num ++;
#endif
			return 0;
		}
	}

	uint8_t major = loc;
	// From start to previous subset
	for (loc = major; loc < SET_ASSOCIATIVE; loc ++) {
		if (sig_match_e(sign, set_header + loc)) {
			tcb_index = calc_index(hash_index, loc);

			// False positive test
			if (is_false_positive(addr, tcb_index)) continue;

			ptr = set_header + loc;
			ptr->signature = 0;
			return 0;
		}
	}
	for (loc = 0; loc < major; loc ++) {
		if (sig_match_e(sign, set_header + loc)) {
			tcb_index = calc_index(hash_index, loc);

			// False positive test
			if (is_false_positive(addr, tcb_index)) continue;

			ptr = set_header + loc;
			ptr->signature = 0;
			return 0;
		}
	}
#else
	for (ptr = set_header, i = 0;
		i < SET_ASSOCIATIVE;
		i ++, ptr ++) {
		
		if (sig_match_e(sign, ptr)) {
			tcb_index = calc_index(hash_index, i);

			// False positive test
			if (is_false_positive(addr, tcb_index)) continue;

			ptr->signature = 0;
			return 0;
		}
	}
#endif

	// Search the collision list
	for (ptr_l = tcp_thread_local_p->conflict_list[hash_index];
		ptr_l != NULL;
		pre_l = ptr_l, ptr_l = ptr_l->next) {
		
		if (sig_match_l(sign, ptr_l)) {
			tcb_index = index_l(ptr_l);

			// False positive test
			if (is_false_positive(addr, tcb_index)) continue;

			if (pre_l == NULL) {
				// The first match, update head
				tcp_thread_local_p->conflict_list[hash_index] = ptr_l->next;
			} else {
				// Link to next
				pre_l->next = ptr_l->next;
			}

#if defined(MEM_LL)
			mem_free(ptr_l, SIZE_LIST_ELEM, tcp_thread_local_p->self_cpu_id);
#else
			free(ptr_l);
#endif

			return tcb_index;
		}
	}
	
	printf("Not found??? What's the matter?????\n");
	exit(0);
	return -1;
}

void
nids_free_tcp_stream(struct tcp_stream *a_tcp)
{
	tcp_context_t *tcp_thread_local_p = pthread_getspecific(tcp_context);

	struct lurker_node *i, *j;
	idx_type tcb_index;

	del_tcp_closing_timeout(a_tcp);
	purge_queue(&a_tcp->server);
	purge_queue(&a_tcp->client);

	if (a_tcp->client.data)
		free(a_tcp->client.data);
	if (a_tcp->server.data)
		free(a_tcp->server.data);

	i = a_tcp->listeners;
	while (i) {
		j = i->next;
		free(i);
		i = j;
	}
#if defined(DEBUG)
	tcp_test[tcp_thread_local_p->self_cpu_id].tcp_num --;
#endif

	tcb_index = delete_from_cache(a_tcp);
	if (tcb_index >= cache_elem_num) {
		bitmap_ret_free_index(tcb_index - cache_elem_num);
	}
	return;
}

int
tcp_init(int size)
{
	tcp_context_t *tcp_thread_local_p = pthread_getspecific(tcp_context);

	int i;
	struct tcp_timeout *tmp;

	// Init bitmap
	bitmap_init(TOTAL_CACHE_ELEM_NUM);

	memset(tcp_test, 0, MAX_CPU_CORES * sizeof(struct test_set));

	// Init cache element number
	cache_elem_num = (SET_NUMBER * SET_ASSOCIATIVE) / (config->worker_num - 1);
	core_elem_num = MAX_STREAM/(config->worker_num - 1);

	// The hash table
	tcp_thread_local_p->tcp_stream_table_size = SET_NUMBER/(config->worker_num - 1);
#if defined(MEM_ALIGN)
	if (0 != posix_memalign((void **)&(tcp_thread_local_p->tcp_stream_table), 64, SET_NUMBER/(config->worker_num - 1) * SET_SIZE)) {
		printf("memalign allocation failed, exit\n");
		exit(0);
	}
	memset(tcp_thread_local_p->tcp_stream_table, 0, SET_NUMBER/(config->worker_num - 1) * SET_SIZE);
#else
	tcp_thread_local_p->tcp_stream_table = calloc(SET_NUMBER/(config->worker_num - 1), SET_SIZE);
#endif
	if (!tcp_thread_local_p->tcp_stream_table) {
		printf("tcp_stream_table in tcp_init");
		exit(0);
		return -1;
	}

#if defined(USE_ULCC)
	int color_range = (tcp_thread_local_p->self_cpu_id - 1) / 2;
	cc_cacheregn_t regn;
	cc_cacheregn_clr(&regn);
	cc_cacheregn_set(&regn, 40 * color_range, 40 * (color_range + 1), 1);
	unsigned long start[1] = {(unsigned long)ULCC_ALIGN_HIGHER((unsigned long)tcp_thread_local_p->tcp_stream_table)};
	unsigned long end[1] = {(unsigned long)ULCC_ALIGN_LOWER((unsigned long)(tcp_thread_local_p->tcp_stream_table + 64 * tcp_thread_local_p->tcp_stream_table_size))};
	if(cc_remap(start, end, 1, &regn, 0, NULL) < 0) {
		printf("WOWOWOWOW, cc_remap failed....... size = %d, cpu id = %d\n", tcp_thread_local_p->tcp_stream_table_size, tcp_thread_local_p->self_cpu_id);
		exit(0);
	}
#endif

	// The conflict Ptr list
#if defined(MEM_ALIGN)
	if (0 != posix_memalign((void **)&(tcp_thread_local_p->conflict_list), 64, SET_NUMBER/(config->worker_num - 1) * PTR_SIZE)) {
		printf("memalign allocation failed, exit\n");
		exit(0);
	}
	memset(tcp_thread_local_p->conflict_list, 0, SET_NUMBER/(config->worker_num - 1) * PTR_SIZE);
#else
	tcp_thread_local_p->conflict_list = calloc(SET_NUMBER/(config->worker_num - 1), PTR_SIZE);
#endif
	if (!tcp_thread_local_p->conflict_list) {
		printf("conflict in tcp_init");
		exit(0);
		return -1;
	}

	// The TCB array
#if defined(MEM_ALIGN)
	if (0 != posix_memalign((void **)&(tcp_thread_local_p->tcb_array), 64, core_elem_num * sizeof(struct tcp_stream))) {
		printf("memalign allocation failed, exit\n");
		exit(0);
	}
	memset(tcp_thread_local_p->tcb_array, 0, core_elem_num * sizeof(struct tcp_stream));
#else
	tcp_thread_local_p->tcb_array = calloc(core_elem_num, sizeof(struct tcp_stream));
#endif
	if (!tcp_thread_local_p->tcb_array) {
		printf("tcp_array in tcp_init");
		exit(0);
		return -1;
	}

	// Following can be optimized
	// init_hash();
	while (tcp_thread_local_p->nids_tcp_timeouts) {
		tmp = tcp_thread_local_p->nids_tcp_timeouts->next;
		free(tcp_thread_local_p->nids_tcp_timeouts);
		tcp_thread_local_p->nids_tcp_timeouts = tmp;
	}
	return 0;
}

// FIXME: Need search the cache table, call corresponding callback function,
// and release resource in this function
void
tcp_exit()
{
	tcp_context_t *tcp_thread_local_p = pthread_getspecific(tcp_context);

	if (!tcp_thread_local_p->tcp_stream_table || !tcp_thread_local_p->tcb_array)
		return;
	free(tcp_thread_local_p->tcb_array);
	free(tcp_thread_local_p->tcp_stream_table);
	free(tcp_thread_local_p->conflict_list);
	tcp_thread_local_p->tcp_stream_table = NULL;
	return;
}

int
process_tcp(u_char * data, int skblen)
{
	tcp_context_t *tcp_thread_local_p = pthread_getspecific(tcp_context);

	struct ip *this_iphdr = (struct ip *)data;
	struct tcphdr *this_tcphdr = (struct tcphdr *)(data + 4 * this_iphdr->ip_hl);
	int datalen, iplen;
	int from_client = 1;
	unsigned int tmp_ts;
	struct tcp_stream *a_tcp;
	struct half_stream *snd, *rcv;

	//  ugly_iphdr = this_iphdr;
	iplen = ntohs(this_iphdr->ip_len);
	if ((unsigned)iplen < 4 * this_iphdr->ip_hl + sizeof(struct tcphdr)) {
#ifdef DEBUG
		printf("process_tcp: iplen < ip + tcp\n");
#endif
		return -1;
	} // ktos sie bawi

	datalen = iplen - 4 * this_iphdr->ip_hl - 4 * this_tcphdr->th_off;

	if (datalen < 0) {
#ifdef DEBUG
		printf("process_tcp: data len < 0\n");
#endif
		return -1;
	} // ktos sie bawi

	if ((this_iphdr->ip_src.s_addr | this_iphdr->ip_dst.s_addr) == 0) {
#ifdef DEBUG
		        printf("process_tcp: ip addrs are both 0\n");
#endif
		return -1;
	}
	//  if (!(this_tcphdr->th_flags & TH_ACK))
	//    detect_scan(this_iphdr);
	if (!nids_params.n_tcp_streams){
#ifdef DEBUG
		printf("process_tcp: nids_params\n");
#endif
		return -1;
	}
	
#if 0
	{
		printf("IN PROCESS_TCP A tcp!, saddr = %d.%d.%d.%d,", 
				this_iphdr->ip_src.s_addr & 0x000000ff,
				(this_iphdr->ip_src.s_addr & 0x0000ff00) >> 8,
				(this_iphdr->ip_src.s_addr & 0x00ff0000) >> 16,
				(this_iphdr->ip_src.s_addr & 0xff000000) >> 24
		      );
		printf("daddr = %d.%d.%d.%d,", 
				this_iphdr->ip_dst.s_addr & 0x000000ff,
				(this_iphdr->ip_dst.s_addr & 0x0000ff00) >> 8,
				(this_iphdr->ip_dst.s_addr & 0x00ff0000) >> 16,
				(this_iphdr->ip_dst.s_addr & 0xff000000) >> 24
		      );
		printf("sport = %d, dport = %d\n", this_tcphdr->th_sport, this_tcphdr->th_dport);
	}
#endif

#if 0
	if (my_tcp_check(this_tcphdr, iplen - 4 * this_iphdr->ip_hl,
				this_iphdr->ip_src.s_addr, this_iphdr->ip_dst.s_addr)) {
		return;
	}
	check_flags(this_iphdr, this_tcphdr);
	//ECN
#endif
	if (!(a_tcp = find_stream(this_tcphdr, this_iphdr, &from_client))) {
		if ((this_tcphdr->th_flags & TH_SYN) &&
				!(this_tcphdr->th_flags & TH_ACK) &&
				!(this_tcphdr->th_flags & TH_RST)){
			add_new_tcp(this_tcphdr, this_iphdr);
			return TCP_SYN_SENT;
		}else{
#ifdef DEBUG
			printf("process_tcp: (syn = 0 || ack = 0) && no tcb\n");
#endif
			return -1;
		}
	}

#if 0
	if (!((a_tcp->addr.source == this_tcphdr->th_sport &&
		a_tcp->addr.dest == this_tcphdr->th_dport &&
		a_tcp->addr.saddr == this_iphdr->ip_src.s_addr &&
		a_tcp->addr.daddr == this_iphdr->ip_dst.s_addr) ||
		(a_tcp->addr.dest == this_tcphdr->th_sport &&
		a_tcp->addr.source == this_tcphdr->th_dport &&
		a_tcp->addr.daddr == this_iphdr->ip_src.s_addr &&
		a_tcp->addr.saddr == this_iphdr->ip_dst.s_addr))) {
		false_positive ++;
	}
#endif

	if (from_client) {
		snd = &a_tcp->client;
		rcv = &a_tcp->server;
	}
	else {
		rcv = &a_tcp->client;
		snd = &a_tcp->server;
	}
	if ((this_tcphdr->th_flags & TH_SYN)) {
		if (from_client || a_tcp->client.state != TCP_SYN_SENT ||
				a_tcp->server.state != TCP_CLOSE || !(this_tcphdr->th_flags & TH_ACK)){
#ifdef DEBUG
			printf("process_tcp: second handshake from client or state error\n");
#endif
			return -1;
		}
		if (a_tcp->client.seq != ntohl(this_tcphdr->th_ack)){
#ifdef DEBUG
			printf("process_tcp: next seq != ack\n");
#endif
			return -1;
		}
		a_tcp->server.state = TCP_SYN_RECV;
		a_tcp->server.seq = ntohl(this_tcphdr->th_seq) + 1;
		a_tcp->server.first_data_seq = a_tcp->server.seq;
		a_tcp->server.ack_seq = ntohl(this_tcphdr->th_ack);
		a_tcp->server.window = ntohs(this_tcphdr->th_win);
		if (a_tcp->client.ts_on) {
			a_tcp->server.ts_on = get_ts(this_tcphdr, &a_tcp->server.curr_ts);
			if (!a_tcp->server.ts_on)
				a_tcp->client.ts_on = 0;
		} else a_tcp->server.ts_on = 0;	
		if (a_tcp->client.wscale_on) {
			a_tcp->server.wscale_on = get_wscale(this_tcphdr, &a_tcp->server.wscale);
			if (!a_tcp->server.wscale_on) {
				a_tcp->client.wscale_on = 0;
				a_tcp->client.wscale  = 1;
				a_tcp->server.wscale = 1;
			}	
		} else {
			a_tcp->server.wscale_on = 0;	
			a_tcp->server.wscale = 1;
		}	
		return TCP_SYN_RECV;
	}
	//  printf("datalen = %d, th_seq = %d, ack_seq = %d, window = %d, wscale = %d\n",
	//	  	datalen, this_tcphdr->th_seq, rcv->ack_seq, rcv->window, rcv->wscale);
	
	if (
			! (  !datalen && ntohl(this_tcphdr->th_seq) == rcv->ack_seq  )
			&&
			( !before(ntohl(this_tcphdr->th_seq), rcv->ack_seq + rcv->window*rcv->wscale) ||
			  before(ntohl(this_tcphdr->th_seq) + datalen, rcv->ack_seq)  
			)
	   )    {
#ifdef DEBUG
		printf("process_tcp: before(),datalen etc\n");
#endif
		return -1;
	}
	
	if ((this_tcphdr->th_flags & TH_RST)) {
		if (a_tcp->nids_state == NIDS_DATA) {
			struct lurker_node *i;

			a_tcp->nids_state = NIDS_RESET;
			for (i = a_tcp->listeners; i; i = i->next)
				(i->item) (a_tcp, &i->data);
		}
		nids_free_tcp_stream(a_tcp);
		return 0;
	}

	/* PAWS check */
	/*
	if (rcv->ts_on && get_ts(this_tcphdr, &tmp_ts) && 
			before(tmp_ts, snd->curr_ts)){
#ifdef DEBUG
		printf("process_tcp: paw check error\n");
#endif
		return -1;
	}
	*/

	if ((this_tcphdr->th_flags & TH_ACK)) {
		if (from_client && a_tcp->client.state == TCP_SYN_SENT &&
				a_tcp->server.state == TCP_SYN_RECV) {
			if (ntohl(this_tcphdr->th_ack) == a_tcp->server.seq) {
				a_tcp->client.state = TCP_ESTABLISHED;
				a_tcp->client.ack_seq = ntohl(this_tcphdr->th_ack);
				{
					struct proc_node *i;
					struct lurker_node *j;
					void *data;

					a_tcp->server.state = TCP_ESTABLISHED;
					a_tcp->nids_state = NIDS_JUST_EST;

#if !defined(DISABLE_UPPER_LAYER)
					for (i = tcp_procs; i; i = i->next) {
						char whatto = 0;
						char cc = a_tcp->client.collect;
						char sc = a_tcp->server.collect;
						char ccu = a_tcp->client.collect_urg;
						char scu = a_tcp->server.collect_urg;

						(i->item) (a_tcp, &data);
						if (cc < a_tcp->client.collect)
							whatto |= COLLECT_cc;
						if (ccu < a_tcp->client.collect_urg)
							whatto |= COLLECT_ccu;
						if (sc < a_tcp->server.collect)
							whatto |= COLLECT_sc;
						if (scu < a_tcp->server.collect_urg)
							whatto |= COLLECT_scu;
						if (nids_params.one_loop_less) {
							if (a_tcp->client.collect >=2) {
								a_tcp->client.collect=cc;
								whatto&=~COLLECT_cc;
							}
							if (a_tcp->server.collect >=2 ) {
								a_tcp->server.collect=sc;
								whatto&=~COLLECT_sc;
							}
						}  
						if (whatto) {
							j = mknew(struct lurker_node);
							j->item = i->item;
							j->data = data;
							j->whatto = whatto;
							j->next = a_tcp->listeners;
							a_tcp->listeners = j;
						}
					}

					if (!a_tcp->listeners) {
						nids_free_tcp_stream(a_tcp);
						return 0;
					}
#endif
					a_tcp->nids_state = NIDS_DATA;
				}
			}
			return TCP_ESTABLISHED;
		}
	}
	if ((this_tcphdr->th_flags & TH_ACK)) {
		handle_ack(snd, ntohl(this_tcphdr->th_ack));
		if (rcv->state == FIN_SENT)
			rcv->state = FIN_CONFIRMED;
		if (rcv->state == FIN_CONFIRMED && snd->state == FIN_CONFIRMED) {
			struct lurker_node *i;

			a_tcp->nids_state = NIDS_CLOSE;
			for (i = a_tcp->listeners; i; i = i->next)
				(i->item) (a_tcp, &i->data);
			nids_free_tcp_stream(a_tcp);
			return 0;
		}
	}
	if (datalen + (this_tcphdr->th_flags & TH_FIN) > 0)
		tcp_queue(a_tcp, this_tcphdr, snd, rcv,
				(char *) (this_tcphdr) + 4 * this_tcphdr->th_off,
				datalen, skblen);
	snd->window = ntohs(this_tcphdr->th_win);
	if (rcv->rmem_alloc > 65535)
		prune_queue(rcv, this_tcphdr);
#if !defined(DISABLE_UPPER_LAYER)
	if (!a_tcp->listeners)
		nids_free_tcp_stream(a_tcp);
#endif
	return 0;
}
#endif
