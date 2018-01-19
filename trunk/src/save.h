#ifndef __SAVE_H_
#define __SAVE_H_

int new_file(void);
void timer_start(void);
void timer_close(void);

void *save_data_thread(void *arg);
void *sd_manage_thread(void *arg);

#endif
