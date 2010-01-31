#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);

/* TODO
   3.3.5 Denying Writes to Executables
   Add code to deny writes to files in use as executables. Many OSes do this 
   because of the unpredictable results if a process tried to run code that 
   was in the midst of being changed on disk. This is especially important 
   once virtual memory is implemented in project 3, but it can't hurt even now.
   You can use file_deny_write() to prevent writes to an open file. Calling 
   file_allow_write() on the file will re-enable them (unless the file is 
   denied writes by another opener). Closing a file will also re-enable 
   writes. Thus, to deny writes to a process's executable, you must keep it 
   open as long as the process is still running.*/


/* read arguments previously pushed on top of user stack, however, this is 
   just a read with give offset, and stack pointer is not changed */
static uint32_t pop (struct intr_frame *f, int offset);

/* add a file into array_files of a thread 
   and allocate a file descriptor on the way*/
static int add_file (struct thread* t, struct file_info* f_info);


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
        /* TODO 
        ?? throw exception here indicating no specified operation */
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
  struct thread *cur = thread_current ();
  cur->thread_exit_status = status;
  thread_exit ();
  return;
}

pid_t
exec (const char *cmd_line)
{
  /* TODO */
  /* What should happen if an exec fails midway through loading?
     exec should return -1 if the child process fails to load for any reason. 
     This includes the case where the load fails part of the way through the 
     process (e.g. where it runs out of memory in the multi-oom test).
     Therefore, the parent process cannot return from the exec system call 
     until it is established whether the load was successful or not. The 
     child must communicate this information to its parent using appropriate 
     synchronization, such as a semaphore (see section A.3.2 Semaphores), to 
     ensure that the information is communicated without race conditions.*/

  /* You must synchronize system calls so that any number of user processes 
     can make them at once. In particular, it is not safe to call into the 
     file system code provided in the filesys directory from multiple threads 
     at once. Your system call implementation must treat the file system code 
     as a critical section. Don't forget that process_execute() also accesses 
     files. For now, we recommend against modifying code in the filesys 
     directory.*/

  /* TODO check address */

  /* execute process
     and load() would happen during the initialization */
  pid_t pid = (pid_t)process_execute (cmd_line);

  if (!checkvaddr (cmd_line))
    {
      struct thread *cur = thread_current ();
      cur->thread_exit_status = -1;
      thread_exit ();     
    }
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
  if (!checkvaddr (file))
    {
      struct thread *cur = thread_current ();
      cur->thread_exit_status = -1;
      thread_exit ();     
    }
  
  lock_acquire (&glb_lock_filesys);
  bool success = filesys_create (file, initial_size);
  lock_release (&glb_lock_filesys);

  return success;
}

bool
remove (const char *file)
{
  /* TODO to discuss here
     according to the project assignment document
     A file may be removed regardless of whether it is open or closed,
     and removing an open file does not close it. */

  /* TODO check address */

  lock_acquire (&glb_lock_filesys);
  bool success = filesys_remove (file);
  lock_release (&glb_lock_filesys);

  return success;
}

int
open (const char *file)
{
  /* TODO Can I set a maximum number of open files per process?
   It is better not to set an arbitrary limit. You may impose 
   a limit of 128 open files per process, if necessary.*/

  /* TODO check address */

  /* open file */
  lock_acquire (&glb_lock_filesys);
  struct file* f_struct = filesys_open (file);
  lock_release (&glb_lock_filesys);

  if (f_struct == NULL)
    {
      return -1;
    }

  /* get current thread */
  struct thread* t = thread_current ();

  /* initialize file_info structure */
  struct file_info* f_info =
    (struct file_info*)malloc (sizeof (struct file_info));

  /* for f_info: initial position */
  f_info->pos = 0;

  /* for f_info: record pointer to file structure */
  f_info->p_file = f_struct;

  /* for f_info: allocate file descriptor, add to array_files */
  lock_acquire (&t->lock_array_files);
  int fd = add_file (t, f_info);
  lock_release (&t->lock_array_files);

  return fd;
}

static int
add_file (struct thread* t, struct file_info* f_info)
{
  int fd = -1;
  for (fd = 2; fd < 128; fd++)
    {
      if (t->array_files[fd] == NULL)   /* available fd number */
        {
          t->array_files[fd] = f_info;  /* add file */
          return fd;                    /* return file descriptor */
        }
    }
  return fd;
}

int
filesize (int fd)
{
  ASSERT (fd >= 2 && fd < 128);

  struct thread* t = thread_current ();

  /* result is file size in bytes */
  lock_acquire (&glb_lock_filesys);
  int result = (int)file_length (t->array_files[fd]->p_file);
  lock_release (&glb_lock_filesys);

  return result;
}

int
read (int fd, void *buffer, unsigned size)
{
  /* TODO check address */

  ASSERT ((fd >= 2 && fd < 128) || (fd == 0));

  /* number of bytes actually read */
  int result = 0;

  if (fd == 0)                  /* read from input */
    {
      int i = 0;
      for (i = 0; i < size; i++)
        {
          *(uint8_t*)buffer = input_getc();
          result++;
          buffer++;/* TODO is it OK to change buffer? */
        }
    }
  else                          /* read from file */
    {
      struct thread* t = thread_current();

      /* get file info */
      lock_acquire (&t->lock_array_files);
      ASSERT (t->array_files[fd] != NULL);

      struct file* pf = t->array_files[fd]->p_file;
      unsigned file_offset = t->array_files[fd]->pos;

      lock_release (&t->lock_array_files);

      /* read and record length of read */
      lock_acquire (&glb_lock_filesys);
      result = file_read_at (pf, buffer, size, file_offset);
      lock_release (&glb_lock_filesys);

      /* advance position within file for current thread */
      lock_acquire (&t->lock_array_files);
      t->array_files[fd]->pos += result;
      lock_release (&t->lock_array_files);
    }

  return result;
}

int
write (int fd, const void *buffer, unsigned size)
{
  /* TODO check address */

  ASSERT ((fd >= 2 && fd < 128) || (fd == 1));

  /* number of bytes actually written */
  int result = 0;

  if (fd == 1)                  /* write to console */
    {
      putbuf (buffer, size);
      result = size;
    }
  else                          /* write to file */
    {
      struct thread* t = thread_current();

      /* get file */
      lock_acquire (&t->lock_array_files);
      ASSERT (t->array_files[fd] != NULL);

      struct file* pf = t->array_files[fd]->p_file;
      unsigned file_offset = t->array_files[fd]->pos;

      lock_release (&t->lock_array_files);

      /* write and record length of read */
      lock_acquire (&glb_lock_filesys);
      result = file_write_at (pf, buffer, size, file_offset);
      lock_release (&glb_lock_filesys);

      /* advance position within file for current thread */
      lock_acquire (&t->lock_array_files);
      t->array_files[fd]->pos += result;
      lock_release (&t->lock_array_files);
    }

  return result;
}

void
seek (int fd, unsigned position)
{
  ASSERT (fd >= 2 && fd < 128);

  struct thread* t = thread_current ();

  lock_acquire (&t->lock_array_files);
  ASSERT (t->array_files[fd] != NULL);

  t->array_files[fd]->pos = position;

  lock_release (&t->lock_array_files);
}

unsigned
tell (int fd)
{
  ASSERT (fd >= 2 && fd < 128);

  struct thread* t = thread_current ();

  lock_acquire (&t->lock_array_files);
  unsigned result = t->array_files[fd]->pos;
  lock_release (&t->lock_array_files);

  return result;
}

void
close (int fd)
{
  /* Unix-like semantics for filesys_remove() are implemented. That is, if a
     file is open when it is removed, its blocks are not deallocated and it 
     may still be accessed by any threads that have it open, until the last 
     one closes it. See Removing an Open File, for more information. */

  /* What happens when an open file is removed?
     You should implement the standard Unix semantics for files. That is, 
     when a file is removed any process which has a file descriptor for that 
     file may continue to use that descriptor. This means that they can read 
     and write from the file. The file will not have a name, and no other 
     processes will be able to open it, but it will continue to exist until 
     all file descriptors referring to the file are closed or the machine 
     shuts down.*/

  ASSERT (fd >= 2 && fd < 128);

  /* get file info, and remove from array_files */
  struct thread* t = thread_current ();
  lock_acquire (&t->lock_array_files);

  ASSERT (t->array_files[fd] != NULL);
  struct file* p_file = t->array_files[fd]->p_file;
  free (t->array_files[fd]);
  t->array_files[fd] = NULL;

  lock_release (&t->lock_array_files);

  /* close file */
  lock_acquire (&glb_lock_filesys);
  file_close (p_file);
  lock_release (&glb_lock_filesys);
}

