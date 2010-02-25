#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "user/syscall.h"
/**/
#include "threads/interrupt.h"
/**/

void syscall_init (void);

void syscall_handler (struct intr_frame *);
mapid_t _mmap (int fd, void *addr);
void _munmap (mapid_t mapping);


#endif /* userprog/syscall.h */
