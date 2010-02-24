#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *cmd_line);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */


void start_process (void *file_name_);
bool setup_stack (void **esp);
bool load_segment (struct file *file, int ofs, uint8_t *upage,  
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);
bool install_page (void *upage, void *kpage, bool writable);
bool load (const char *cmd_line, void (**eip) (void), void **esp);
