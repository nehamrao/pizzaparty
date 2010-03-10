#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "filesys/cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  struct thread *t = thread_current ();
  struct dir *dir;
  if (name[0] == '/' || t->current_dir == NULL)
    dir = dir_open_root ();
  else
    dir = dir_reopen (t->current_dir);

  char *file_name = malloc (strlen(name) + 1);
  if (file_name == NULL)
  {
    dir_close (dir);
    return false;
  }
  strlcpy (file_name, name, strlen(name) + 1);
 
  char *token1, *token2, *save_ptr;
  struct inode *inode = NULL;
  bool success = true;

  token1 = strtok_r (file_name, "/", &save_ptr);
  for (token2 = strtok_r (NULL, "/", &save_ptr); token2 != NULL; token2 = strtok_r (NULL, "/", &save_ptr))
  {
    success = dir_lookup (dir, token1, &inode);
    dir_close (dir);
    if (!success) 
      return false;     

    dir = dir_open (inode);
    token1 = token2;
  }
 
  if (strlen (token1) > NAME_MAX)
    return false; 
 
  bool success1, success2, success3;
  if (success)
  {
    success1 = free_map_allocate (1, &inode_sector);
    success2 = inode_create (inode_sector, initial_size, false);
    success3 = dir_add (dir, token1, inode_sector);
//    printf ("success %d %d %d \n", success1, success2, success3);
    success = (dir != NULL && success1 && success2 && success3);
                /*  && free_map_allocate (1, &inode_sector) 
                  && inode_create (inode_sector, initial_size, 0)
                  && dir_add (dir, token1, inode_sector));*/
//  printf ("success = %ld, filecreate inode_sector = %ld\n", success, inode_sector);
  }
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  dir_close (dir);
 
  if (file_name != NULL)
    free (file_name);

  return success;
}


struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
}


/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file_info * 
filesys_open_file (const char *name_)
{
  struct thread *t = thread_current ();
  struct dir *dir; 
  struct inode *inode = NULL;
  char *token1, *save_ptr;
  struct file_info * f_info = (struct file_info *) malloc (sizeof (struct file_info));
  char name [strlen (name_) + 1];
  strlcpy (name, name_, strlen(name_)+1);

  if (name[0] == '/')
    dir = dir_open_root ();     
  else
    dir = dir_reopen(t->current_dir);

  token1 = strtok_r (name, "/", &save_ptr);
       
  if (token1 == NULL)
    inode = inode_open (ROOT_DIR_SECTOR);
  else 
    dir_lookup (dir, token1, &inode);
       
  dir_close (dir);

  if (inode == NULL)
  {
    return NULL;
  }
    
  off_t isdir = inode_isdir (inode);

  if (isdir)
  {
    f_info->p_dir = dir_open (inode);
    f_info->p_file = NULL;
  }
  else
  {
    f_info->p_file = file_open (inode);
    f_info->p_dir = NULL; 
  }
  return f_info;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct thread * t = thread_current ();
  struct dir *dir;
  bool success = false;

  if (name[0] == '/')
    dir = dir_open_root ();     
  else
    dir = dir_reopen(t->current_dir);

  char *token1, *token2, *save_ptr;  
  char * file_name = malloc(strlen (name) + 1);
  if (file_name == NULL)
  {
    dir_close (dir);
    return false;    
  }
  strlcpy (file_name, name, strlen (name) + 1);

  token1 = strtok_r (file_name, "/", &save_ptr);
  for (token2 = strtok_r (NULL, "/", &save_ptr); token2 != NULL; token2 = strtok_r (NULL, "/", &save_ptr))
  {
    struct inode *inode = NULL;
    success = dir_lookup (dir, token1, &inode);     
    if (!success)
    {
      dir_close (dir);
      return false;
    }
     
    dir_close (dir);
    dir = dir_open (inode);   
    token1 = token2;
  }
  
  if (token1 != NULL)
    success = dir_remove (dir, token1);

  dir_close (dir); 
  return success;
}

  
/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR, 20))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
