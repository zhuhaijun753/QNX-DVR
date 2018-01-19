#ifndef __AVB_H_
#define __AVB_H_

#include <net/bpf.h>

int avb_init(void);
struct avb_buffer *get_avb_recv_buf(void);

int avb_recv_signal(int lock);
void *avb_recv_thread(void *arg);

#endif
