#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

/* yinfeng ******************************************************************/
static uint32_t pop (struct intr_frame *f);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  /* get syscall number */
  int syscall_no = (int)(pop (f));

  /* dispatch to individual calls */
  uint32_t arg1;
  uint32_t arg2;
  uint32_t arg3;
  switch (syscall_no)
    {
      case SYS_HALT:
        halt ();
        break;
      case SYS_EXIT:
        arg1 = pop (f);
        exit ((int)arg1);
        break;
      case SYS_EXEC:
        arg1 = pop (f);
        f->eax = (uint32_t)exec ((const char*)arg1);
        break;
      case SYS_WAIT:
        arg1 = pop (f);
        f->eax = (uint32_t)wait ((int)arg1);
        break;
      case SYS_CREATE:
        arg1 = pop (f);
        arg2 = pop (f);
        f->eax = (uint32_t)create ((const char*)arg1, (unsigned)arg2);
        break;
      case SYS_REMOVE:
        arg1 = pop (f);
        f->eax = (uint32_t)remove ((const char*)arg1);
        break;
      case SYS_OPEN:
        arg1 = pop (f);
        f->eax = (uint32_t)open ((const char*)arg1);
        break;
      case SYS_FILESIZE:
        arg1 = pop (f);
        f->eax = (uint32_t)filesize ((int)arg1);
        break;
      case SYS_READ:
        arg1 = pop (f);
        arg2 = pop (f);
        arg3 = pop (f);
        f->eax = (uint32_t)read ((int)arg1, (void*)arg2, (unsigned)arg3);
        break;
      case SYS_WRITE:
        arg1 = pop (f);
        arg2 = pop (f);
        arg3 = pop (f);
        f->eax = (uint32_t)write((int)arg1, (const void*)arg2, (unsigned)arg3);
        break;
      case SYS_SEEK:
        arg1 = pop (f);
        arg2 = pop (f);
        seek ((int)arg1, (unsigned)arg2);
        break;
      case SYS_TELL:
        arg1 = pop (f);
        f->eax = (uint32_t)tell ((int)arg1);
        break;
      case SYS_CLOSE:
        arg1 = pop (f);
        close ((int)arg1);
        break;
      default:
        /* we should throw exception here indicating no specified operation */
        break;
    }
}

static uint32_t pop (struct intr_frame *f)
{
  uint32_t result = *(uint32_t *)f->esp;
  f->esp += 4;

  return result;
}

void
halt (void)
{}

void
exit (int status)
{
  thread_exit ();
  return;
}

pid_t
exec (const char *cmd_line)
{
  return 0;
}

int
wait (pid_t pid)
{
  return 0;
}

bool
create (const char *file, unsigned initial_size)
{
  return false;
}

bool
remove (const char *file)
{
  return true;
}

int
open (const char *file)
{
  return 0;
}

int
filesize (int fd)
{
  return 0;
}

int
read (int fd, void *buffer, unsigned size)
{
  return 0;
}

int
write (int fd, const void *buffer, unsigned size)
{
  if (!checkvaddr (buffer))
    exit (-1); // Need to change
  ASSERT ( fd == 1);
  putbuf (buffer, size);
  return size;
}

void
seek (int fd, unsigned position)
{}

unsigned
tell (int fd)
{
  return 0;
}

void
close (int fd)
{}

/* yinfeng ******************************************************************/
