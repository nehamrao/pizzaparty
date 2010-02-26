#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include <round.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/pte.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"

static void syscall_handler (struct intr_frame *);

/*** static methods providing service to lib/user/syscall.h */
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
/*** static methods providing utility functions to above methods */

/* determine a valid virtual address given from user */
static bool checkvaddr(const void * vaddr, unsigned size);

/* determine if a valid user file descriptor */
static bool is_user_fd (int fd);

/* read arguments previously pushed on top of user stack, note this is 
   just a read with give offset, and stack pointer is not changed */
static uint32_t read_stack (struct intr_frame *f, int offset);

/* Add a file into array_files of a thread 
   and allocate a file descriptor on the way*/
static int add_file (struct thread* t, struct file_info* f_info);

/* Kill a process and exit with status -1 */
static void kill_process (void);

/* allocate a new mmap file id */
static mapid_t allocate_mapid (void);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void
syscall_handler (struct intr_frame *f) 
{
  /* Get syscall number */
  int syscall_no = (int)(read_stack (f, 0));
  
  thread_current ()->user_esp = f->esp;

  /* Dispatch to individual calls */
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

      case SYS_MMAP:
        arg1 = read_stack (f, 4);
        arg2 = read_stack (f, 8);
        f->eax = (uint32_t)_mmap ((int)arg1, (void*)arg2);
        break;

      case SYS_MUNMAP:
        arg1 = read_stack (f, 4);
        _munmap ((mapid_t)arg1);
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

  if (position > (unsigned)_filesize (fd)) 
    position = (unsigned)_filesize (fd);

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

mapid_t
_mmap (int fd, void *addr)
{
  struct thread* t = thread_current ();

  /* All the fail input conditions */
  if (!is_user_vaddr (addr) ||          /* not valid user addr */
      pg_ofs (addr) != 0 ||             /* not page aligned */
      addr == 0 ||                      /* at virtual address 0 */
      fd == STDIN_FILENO ||             /* stdin */
      fd == STDOUT_FILENO ||            /* stdout */
      _filesize (fd) == 0)              /* length file 0 */
    {
      /* Fail operation */
      return MAP_FAILED;
    }

  /* Also fail if overlap occurs */
  void *add = NULL;
  int f_size = _filesize (fd);
  for (add = addr; add < addr + f_size; add += PGSIZE)
    {
      /* The existence of both pte and ps indicates an already used page */
      uint32_t *pte = sup_pt_pte_lookup (t->pagedir, add, false);
      if (pte != NULL)
        {
          if (sup_pt_ps_lookup (pte) != NULL)
            {
              /* Fail operation */
              return MAP_FAILED;
            }
        }
    }
  
  /* Allocate mapid */
  mapid_t mapid = allocate_mapid ();
  struct mmap_struct* ms =
    (struct mmap_struct*)malloc (sizeof (struct mmap_struct));
  if (ms == NULL)
    {
      /* Fail operation */
      return MAP_FAILED;
    }
  ms->mapid = mapid;
  ms->vaddr = addr;

  /* Reopen to get an independent reference to the file */
  lock_acquire (&glb_lock_filesys);

  struct file* new_file_ref = file_reopen (t->array_files[fd]->p_file);

  lock_release (&glb_lock_filesys);
  if (new_file_ref == NULL)
    {
      /* Fail operation */
      free (ms);
      return MAP_FAILED;
    }
  ms->p_file = new_file_ref;

  /* Record in mmap_list */
  list_push_back (&t->mmap_list, &ms->elem);

  /* Begin mapping, creating sup_pt entries */
  uint32_t read_bytes = f_size;
  uint32_t zero_bytes = ROUND_UP (read_bytes, PGSIZE) - read_bytes;
  void* upage = addr;
  block_sector_t sector_idx = byte_to_sector (file_get_inode (ms->p_file), 0);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Add sup_pt entry */
      uint32_t flag =
        (page_read_bytes > 0 ? 0 : FS_ZERO) | POS_DISK | TYPE_MMFile;
      mark_page (upage, NULL, page_read_bytes, flag, sector_idx);

      /* Advance */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      sector_idx += PGSIZE / BLOCK_SECTOR_SIZE;
    }

  return mapid;
}

void
_munmap (mapid_t mapping)
{
  struct thread* t = thread_current ();
  struct list_elem* e = NULL;
  struct list_elem* next_elem = NULL;
  uint32_t f_size = 0;

  /* Traverse list to find the mapping record */
  for (e = list_begin (&t->mmap_list); e != list_end (&t->mmap_list);
       e = next_elem)
    {
      struct mmap_struct* ms = list_entry (e, struct mmap_struct, elem);
      if (ms->mapid == mapping)
        {
          /* Release pages, write back to disk in the process */
          f_size = file_length (ms->p_file);
          uint32_t write_bytes = f_size;
          void* upage = ms->vaddr;
          while (write_bytes > 0)
            {
              /* Calculate how to fill this page */
              size_t page_write_bytes =
                write_bytes < PGSIZE ? write_bytes : PGSIZE;

              /* Write back to disk if page is dirty */
              uint32_t* pte = sup_pt_pte_lookup (t->pagedir, upage, false);
              struct page_struct* ps = sup_pt_ps_lookup (pte);
              if (sup_pt_fs_is_dirty (ps->fs))
                {
                  file_write_at (ms->p_file, upage, write_bytes,
                                 upage - ms->vaddr);
                }

              /* Delete pte and release frame */
              uint32_t tmp_pte_content = *pte;
              if (sup_pt_delete (pte))
                {
                  if ((tmp_pte_content & PTE_P) != 0)
                    palloc_free_page (pte_get_page (tmp_pte_content));
                }

              /* Advance  */
              write_bytes -= page_write_bytes;
              upage += PGSIZE;
            }

          /* Remove this mapping record */
          file_close (ms->p_file);
          list_remove (e);
          free (ms);
          break;
        }
      else
        {
          next_elem = list_next (e);
        }
    }
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

/* Add a file to the open file arrays for a process */
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
  /* If the address exceeds PHYS_BASE, exit -1 */
  if (!is_user_vaddr (vaddr + size)) 
    return false;

  return true;
}

/* Check the validity of the user file descriptor */
static bool
is_user_fd (int fd)
{
   struct thread *t = thread_current ();

   return ((fd >= 2) && (fd < 128) && (t->array_files[fd] != NULL));
}

/* allocate a new mmap file id */
static mapid_t
allocate_mapid (void)
{
  struct thread *t = thread_current ();

  mapid_t result = t->next_mapid;
  t->next_mapid++;

  return result;
}

