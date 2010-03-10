#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "user/syscall.h"

void syscall_init (void);

bool _mkdir (const char *dir);
int _write (int fd, const void *buffer, unsigned size);
int _open (const char *file);
bool _readdir (int fd, char *name);
#endif /* userprog/syscall.h */
