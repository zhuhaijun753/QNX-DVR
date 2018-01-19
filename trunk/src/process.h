#ifndef __PROCESS_H_
#define __PROCESS_H_

int save_buf_init(void);
void set_s_flag(int flag);
void reset_sn(void);

struct save_buffer *get_save_data(void);
void *avb_proc_thread(void *arg);

#endif
