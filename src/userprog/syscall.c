#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

/* yinfeng ******************************************************************/
static uint32_t pop (struct intr_frame *f, int offset);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  /* get syscall number */
  int syscall_no = (int)(pop (f, 0));

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
        arg1 = pop (f, 4);
        exit ((int)arg1);
        break;
      case SYS_EXEC:
        arg1 = pop (f, 4);
        f->eax = (uint32_t)exec ((const char*)arg1);
        break;
      case SYS_WAIT:
        arg1 = pop (f, 4);
        f->eax = (uint32_t)wait ((int)arg1);
        break;
      case SYS_CREATE:
        arg1 = pop (f, 4);
        arg2 = pop (f, 8);
        f->eax = (uint32_t)create ((const char*)arg1, (unsigned)arg2);
        break;
      case SYS_REMOVE:
        arg1 = pop (f, 4);
        f->eax = (uint32_t)remove ((const char*)arg1);
        break;
      case SYS_OPEN:
        arg1 = pop (f, 4);
        f->eax = (uint32_t)open ((const char*)arg1);
        break;
      case SYS_FILESIZE:
        arg1 = pop (f, 4);
        f->eax = (uint32_t)filesize ((int)arg1);
        break;
      case SYS_READ:
        arg1 = pop (f, 4);
        arg2 = pop (f, 8);
        arg3 = pop (f, 12);
        f->eax = (uint32_t)read ((int)arg1, (void*)arg2, (unsigned)arg3);
        break;
      case SYS_WRITE:
        arg1 = pop (f, 4);
        arg2 = pop (f, 8);
        arg3 = pop (f, 12);
        f->eax = (uint32_t)write ((int)arg1, (const void*)arg2, (unsigned)arg3);
        break;
      case SYS_SEEK:
        arg1 = pop (f, 4);
        arg2 = pop (f, 8);
        seek ((int)arg1, (unsigned)arg2);
        break;
      case SYS_TELL:
        arg1 = pop (f, 4);
        f->eax = (uint32_t)tell ((int)arg1);
        break;
      case SYS_CLOSE:
        arg1 = pop (f, 4);
        close ((int)arg1);
        break;
      default:
        /* we should throw exception here indicating no specified operation */
        break;
    }
}

static uint32_t pop (struct intr_frame *f, int offset)
{
  return *(uint32_t *)(f->esp + offset);
}

void
halt (void)
{
  shutdown_power_off ();
}

void
exit (int status)
{
    thread_exit ();
    // More to be added... Returning status.
}

pid_t
exec (const char *cmd_line)
{
  pid_t pid = (pid_t)process_execute (cmd_line);

  struct thread* t = thread_current ();
  sema_down (&t->sema_child_load);
  if (t->child_load_success)
    {
      return pid;
    }
  else
    {
      return -1;
    }
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
  ASSERT ( fd == 1);
  putbuf (buffer, size);
  return size;
//>>>>>>> .r11
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
