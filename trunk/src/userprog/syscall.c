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
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);

/* static methods providing service to lib/user/syscall.h */
static void _halt (void);
static void _exit (int status);
static pid_t _exec (const char *cmd_line);
static int _wait (pid_t pid);
static bool _create (const char *file, unsigned initial_size);
static bool _remove (const char *file);
static int _open (const char *file);
static int _filesize (int fd);
static int _read (int fd, void *buffer, unsigned size);
static int _write (int fd, const void *buffer, unsigned size);
static void _seek (int fd, unsigned position);
static unsigned _tell (int fd);
static void _close (int fd);


/* static methods providing utility functions to above methods */

/* read arguments previously pushed on top of user stack, note this is 
   just a read with give offset, and stack pointer is not changed */
static uint32_t read_stack (struct intr_frame *f, int offset);

/* add a file into array_files of a thread 
   and allocate a file descriptor on the way*/
static int add_file (struct thread* t, struct file_info* f_info);

/* kill a process and exit with status -1 */
static void kill_process (void);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  /* get syscall number */
  int syscall_no = (int)(read_stack (f, 0));

  /* dispatch to individual calls */
  uint32_t arg1, arg2, arg3;
  switch (syscall_no)
    {
      case SYS_HALT:
        _halt ();
        break;

      case SYS_EXIT:
        arg1 = read_stack (f, 4);
        _exit ((int)arg1);
        break;

      case SYS_EXEC:
        arg1 = read_stack (f, 4);
        f->eax = (uint32_t)_exec ((const char*)arg1);
        break;

      case SYS_WAIT:
        arg1 = read_stack (f, 4);
        f->eax = (uint32_t)_wait ((int)arg1);
        break;

      case SYS_CREATE:
        arg1 = read_stack (f, 4);
        arg2 = read_stack (f, 8);
        f->eax = (uint32_t)_create ((const char*)arg1, (unsigned)arg2);
        break;

      case SYS_REMOVE:
        arg1 = read_stack (f, 4);
        f->eax = (uint32_t)_remove ((const char*)arg1);
        break;

      case SYS_OPEN:
        arg1 = read_stack (f, 4);
        f->eax = (uint32_t)_open ((const char*)arg1);
        break;

      case SYS_FILESIZE:
        arg1 = read_stack (f, 4);
        f->eax = (uint32_t)_filesize ((int)arg1);
        break;

      case SYS_READ:
        arg1 = read_stack (f, 4);
        arg2 = read_stack (f, 8);
        arg3 = read_stack (f, 12);
        f->eax = (uint32_t)_read ((int)arg1, (void*)arg2, (unsigned)arg3);
        break;

      case SYS_WRITE:
        arg1 = read_stack (f, 4);
        arg2 = read_stack (f, 8);
        arg3 = read_stack (f, 12);
        f->eax = (uint32_t)_write ((int)arg1, (const void*)arg2, (unsigned)arg3);
        break;

      case SYS_SEEK:
        arg1 = read_stack (f, 4);
        arg2 = read_stack (f, 8);
        _seek ((int)arg1, (unsigned)arg2);
        break;

      case SYS_TELL:
        arg1 = read_stack (f, 4);
        f->eax = (uint32_t)_tell ((int)arg1);
        break;

      case SYS_CLOSE:
        arg1 = read_stack (f, 4);
        _close ((int)arg1);
        break;

      default:
        kill_process ();    
        break;
    }
}



static void
_halt (void)
{
  shutdown_power_off ();
}

static void
_exit (int status)
{
  struct thread *cur = thread_current ();
  cur->info->exit_status = status;
  thread_exit ();
}

static pid_t
_exec (const char *cmd_line)
{
  /* check address */
  if (!checkvaddr (cmd_line))
    {
      kill_process();
    }

  /* begin executing */
  pid_t pid = (pid_t) process_execute (cmd_line);

  /* wait to receive message about child loading success */
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

static int
_wait (pid_t pid)
{
  return process_wait (pid);
}

static bool
_create (const char *file, unsigned initial_size)
{
  /* check address */
  if (!checkvaddr (file))
    {
      kill_process();
    }

  /* protected filesys operation: create file */
  lock_acquire (&glb_lock_filesys);
  bool success = filesys_create (file, initial_size);
  lock_release (&glb_lock_filesys);

  return success;
}

static bool
_remove (const char *file)
{
  /* check address */
  if (!checkvaddr (file))
    {
      kill_process();
    }

  /* protected filesys operation: remove file */
  lock_acquire (&glb_lock_filesys);
  bool success = filesys_remove (file);
  lock_release (&glb_lock_filesys);

  return success;
}

static int
_open (const char *file)
{
  /* check address */
  if (!checkvaddr (file))
    {
      kill_process();
    }

  /* protected filesys operation: open file */
  lock_acquire (&glb_lock_filesys);
  struct file* f_struct = filesys_open (file);
  lock_release (&glb_lock_filesys);

  if (f_struct == NULL)
    {
      return -1;
    }

  /* initialize file_info structure */
  struct file_info* f_info =
    (struct file_info*)malloc (sizeof (struct file_info));

  /* for f_info: initial position */
  f_info->pos = 0;

  /* for f_info: record pointer to file structure */
  f_info->p_file = f_struct;

  /* for f_info: allocate file descriptor, add to array_files */
  struct thread* t = thread_current ();
  lock_acquire (&t->lock_array_files);
  int fd = add_file (t, f_info);
  lock_release (&t->lock_array_files);

  return fd;
}



static int
_filesize (int fd)
{
  /* check file descriptor */
  if (fd < 2 || fd >= 128)
    {
      kill_process ();
    }

  /* protected filesys operation: get file size */
  struct thread* t = thread_current ();
  lock_acquire (&glb_lock_filesys);
  int result = (int)file_length (t->array_files[fd]->p_file);
  lock_release (&glb_lock_filesys);

  /* result is file size in bytes */
  return result;
}

static int
_read (int fd, void *buffer, unsigned size)
{
  /* check address */
  if (!checkvaddr (buffer))
    {
      kill_process();
    }

  /* check file descriptor */
  if ((fd < 2 || fd >= 128) && (fd != STDIN_FILENO))
    {
      kill_process ();
    }

  /* number of bytes actually read */
  int result = 0;

  if (fd == STDIN_FILENO)       /* read from input */
    {
      unsigned i = 0;
      for (i = 0; i < size; i++)
        {
          *(uint8_t*)buffer = input_getc();
          result++;
          buffer++;
        }
    }
  else                          /* read from file */
    {
      /* get file info */
      struct thread* t = thread_current();
      lock_acquire (&t->lock_array_files);
      if (t->array_files[fd] == NULL)
        {
          kill_process ();
        }

      struct file* pf = t->array_files[fd]->p_file;
      unsigned file_offset = t->array_files[fd]->pos;

      lock_release (&t->lock_array_files);

      /* protected filesys operation:
         read and record length of read */
      lock_acquire (&glb_lock_filesys);
      result = file_read_at (pf, buffer, size, file_offset);
      lock_release (&glb_lock_filesys);

      /* increment position within file for current thread */
      lock_acquire (&t->lock_array_files);
      t->array_files[fd]->pos += result;
      lock_release (&t->lock_array_files);
    }

  return result;
}

static int
_write (int fd, const void *buffer, unsigned size)
{
  /* check address */
  if (!checkvaddr (buffer))
    {
      kill_process();
    }

  /* check file descriptor */
  if ((fd < 2 || fd >= 128) && (fd != STDOUT_FILENO))
    {
      kill_process ();
    }

  /* number of bytes actually written */
  int result = 0;

  if (fd == STDOUT_FILENO)      /* write to console */
    {
      putbuf (buffer, size);
      result = size;
    }
  else                          /* write to file */
    {
      /* get file */
      struct thread* t = thread_current();
      lock_acquire (&t->lock_array_files);
      if (t->array_files[fd] == NULL)
        {
          kill_process ();
        }

      struct file* pf = t->array_files[fd]->p_file;
      unsigned file_offset = t->array_files[fd]->pos;

      lock_release (&t->lock_array_files);

      /* protected filesys operation:
         write and record length of read */
      lock_acquire (&glb_lock_filesys);
      result = file_write_at (pf, buffer, size, file_offset);
      lock_release (&glb_lock_filesys);

      /* increment position within file for current thread */
      lock_acquire (&t->lock_array_files);
      t->array_files[fd]->pos += result;
      lock_release (&t->lock_array_files);
    }

  return result;
}

static void
_seek (int fd, unsigned position)
{
  /* check file descriptor */
  if (fd < 2 || fd >= 128)
    {
      kill_process ();
    }

  /* seek to desired position */
  struct thread* t = thread_current ();
  lock_acquire (&t->lock_array_files);
  if (t->array_files[fd] == NULL)
    {
      kill_process ();
    }

  t->array_files[fd]->pos = position;

  lock_release (&t->lock_array_files);
}

static unsigned
_tell (int fd)
{
  /* check file descriptor */
  if (fd < 2 || fd >= 128)
    {
      kill_process ();
    }

  /* tell current position */
  struct thread* t = thread_current ();
  lock_acquire (&t->lock_array_files);

  unsigned result = t->array_files[fd]->pos;

  lock_release (&t->lock_array_files);

  return result;
}

static void
_close (int fd)
{
  /* check file descriptor */
  if (fd < 2 || fd >= 128)
    {
      kill_process ();
    }

  /* get file info, and remove from array_files */
  struct thread* t = thread_current ();
  lock_acquire (&t->lock_array_files);

  if (t->array_files[fd] == NULL)
    {
      kill_process ();
    }
  struct file* p_file = t->array_files[fd]->p_file;
  free (t->array_files[fd]);
  t->array_files[fd] = NULL;

  lock_release (&t->lock_array_files);

  /* protectec filesys operation: close file */
  lock_acquire (&glb_lock_filesys);
  file_close (p_file);
  lock_release (&glb_lock_filesys);
}


/* utility methods */

static uint32_t
read_stack (struct intr_frame *f, int offset)
{
  /* check address */
  if (!checkvaddr (f->esp + offset))
    {
      kill_process();
    }

  return *(uint32_t *)(f->esp + offset);
}

static void
kill_process ()
{
  struct thread *cur = thread_current ();
  cur->info->exit_status = -1;
  thread_exit ();     
}

static int
add_file (struct thread* t, struct file_info* f_info)
{
  int fd = -1;
  for (fd = 2; fd < 128; fd++)
    {
      /* found available file descriptor number */
      if (t->array_files[fd] == NULL)
        {
          /* add file */
          t->array_files[fd] = f_info;

          /* return file descriptor */
          return fd;
        }
    }
  return fd;
}

