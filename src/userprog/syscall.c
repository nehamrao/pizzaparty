#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
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
static bool checkvaddr(const void * vaddr, unsigned size);
static bool is_user_fd (int fd);

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
  cur->process_info->exit_status = status;
  thread_exit ();
}

static pid_t
_exec (const char *cmd_line)
{
  /* Check address */
  if (!checkvaddr (cmd_line, 0) || !checkvaddr(cmd_line, strlen(cmd_line)))
  {
      kill_process();
  }

  /* Begin executing */
  pid_t pid = (pid_t) process_execute (cmd_line);

  /* If pid allocation fails, exit -1 */
  if (pid == -1) 
    return -1;

  /* Wait to receive message about child loading success */
  struct thread* t = thread_current ();
  sema_down (&t->process_info->sema_load);

  if (t->process_info->child_load_success)
      return pid;
  else
      return -1;
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
  if (!checkvaddr (file, 0) || !checkvaddr (file, strlen(file)))
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
  if (!checkvaddr (file, 0) || !checkvaddr (file, strlen(file)))
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
  if (!checkvaddr (file, 0) || !checkvaddr (file, strlen(file)))
    {
      kill_process();
    }

  /* protected filesys operation: open file */
  lock_acquire (&glb_lock_filesys);
  struct file* f_struct = filesys_open (file);
  lock_release (&glb_lock_filesys);

  /* If open fails, return -1 */
  if (f_struct == NULL)
    {
      return -1;
    }

  /* Initialize file_info structure */
  struct file_info* f_info =
    (struct file_info *) malloc (sizeof (struct file_info));

  /* for f_info: initial position */
  f_info->pos = 0;

  /* for f_info: record pointer to file structure */
  f_info->p_file = f_struct;

  /* for f_info: allocate file descriptor, add to array_files */
  struct thread* t = thread_current ();
  int fd = add_file (t, f_info);

  return fd;
}

static int
_filesize (int fd)
{
  /* Check file descriptor */
  if (!is_user_fd (fd))
    kill_process ();

  /* Protect filesys operation: get file size */
  struct thread* t = thread_current ();
  lock_acquire (&glb_lock_filesys);
  int retval = (int) file_length (t->array_files[fd]->p_file);
  lock_release (&glb_lock_filesys);

  /* Retval is file size in bytes */
  return retval;
}

static int
_read (int fd, void *buffer, unsigned size)
{
  /* Check address and file descriptor*/
  if (!checkvaddr (buffer, size) || (!is_user_fd (fd) && (fd != STDIN_FILENO)))
    {
      kill_process();
    }

  /* Result is the number of bytes actually read */
  int result = 0;

  if (fd == STDIN_FILENO)       /* Read from input */
    {
      unsigned i = 0;
      for (i = 0; i < size; i++)
        {
          *(uint8_t *)buffer = input_getc();
          result++;
          buffer++;
        }
    }
  else                          /* Read from file */
    {
      /* Get file info */
      struct thread* t = thread_current();

      struct file* pf = t->array_files[fd]->p_file;
      unsigned file_offset = t->array_files[fd]->pos;

      /* protected filesys operation:
         read and record length of read */
      lock_acquire (&glb_lock_filesys);
      result = file_read_at (pf, buffer, size, file_offset);

      /* increment position within file for current thread */
      t->array_files[fd]->pos += result;

      lock_release (&glb_lock_filesys);
    }
  return result;
}

static int
_write (int fd, const void *buffer, unsigned size)
{
  /* Check address and file descriptor*/
  if (!checkvaddr(buffer, size) || (!is_user_fd (fd) && (fd != STDOUT_FILENO)))
    {
      kill_process();
    }

  /* Result is the number of bytes actually written */
  int result = 0;

  if (fd == STDOUT_FILENO)      /* Write to console */
    {
      putbuf (buffer, size);
      result = size;
    }
  else                          /* Write to file */
    {
      struct thread* t = thread_current();

      /* Get file info*/
      struct file* pf = t->array_files[fd]->p_file;
      unsigned file_offset = t->array_files[fd]->pos;

      /* Protect filesys operation:
         write and record length of read */
      lock_acquire (&glb_lock_filesys);
      result = file_write_at (pf, buffer, size, file_offset);

      /* Increment position within file for current thread */
      t->array_files[fd]->pos += result;
      lock_release (&glb_lock_filesys);
    }

  return result;
}

static void
_seek (int fd, unsigned position)
{
  /* Check file descriptor */
  if (!is_user_fd (fd))
    {
      kill_process ();
    }

  if (position < 0) 
    position = 0;
  if (position > _filesize (fd)) 
    position = _filesize (fd);

  /* Seek to desired position */
  struct thread* t = thread_current ();
  t->array_files[fd]->pos = position;
}

static unsigned
_tell (int fd)
{
  /* Check file descriptor */
  if (!is_user_fd (fd))
    {
      kill_process ();
    }

  /* Tell current position */
  struct thread* t = thread_current ();
  return t->array_files[fd]->pos;
}

static void
_close (int fd)
{
  /* Check file descriptor */
  if (!is_user_fd (fd))
    {
      kill_process ();
    }

  /* Get file info, and remove from array_files */
  struct thread* t = thread_current ();

  struct file* p_file = t->array_files[fd]->p_file;

  /* Protect filesys operation: close file */
  lock_acquire (&glb_lock_filesys);

  free (t->array_files[fd]);
  t->array_files[fd] = NULL;

  file_close (p_file);
  lock_release (&glb_lock_filesys);
}


/* Utility functions */

static uint32_t
read_stack (struct intr_frame *f, int offset)
{
  /* Check address */
  if (!checkvaddr (f->esp + offset, 0))
      kill_process();

  return *(uint32_t *)(f->esp + offset);
}

/* Kill the process due to abnormal behavior */
static void
kill_process ()
{
  _exit (-1);
}

static int
add_file (struct thread* t, struct file_info* f_info)
{
  int fd;
  for (fd = 2; fd < 128; fd++)
    {
      /* Find available file descriptor number */
      if (t->array_files[fd] == NULL)
        {
          /* Add file */
          t->array_files[fd] = f_info;

          /* Return file descriptor */
          return fd;
        }
    }
  return fd;
}

/* Check validity of buffer starting at vaddr, with length of size*/
static bool
checkvaddr(const void * vaddr, unsigned size)
{
  uint32_t *pcheck;

  struct thread *t = thread_current ();

  /* If the address exceeds PHYS_BASE, exit -1 */
  if (!is_user_vaddr (vaddr + size)) 
    return false;

  /* Check if every page is mapped */
  for (pcheck = pg_round_down (vaddr); 
       pcheck <= pg_round_down (vaddr + size);)
       {
         if (!pagedir_get_page (t->pagedir, pcheck))
           return false;
         pcheck += PGSIZE;      
       } 
  return true;
}

/* Check the validity of the user file descriptor */
static bool
is_user_fd (int fd)
{
   struct thread *t = thread_current ();

   return ((fd >= 2) && (fd < 128) && (t->array_files[fd] != NULL));
}


