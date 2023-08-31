#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <tanc.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
#include "threads/thread.h"

#ifdef FS
#include "filesys/cache.h"
#endif

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
#ifdef FS
  cache_write_behind(true);
#endif

  free_map_close ();
}

struct path_elem
{
  char name[NAME_MAX + 1];
  struct list_elem elem;
};

static void
cleanup_path (struct list* path_list)
{
  while (!list_empty (path_list))
  {
    struct list_elem* e = list_pop_front (path_list);
    struct path_elem* pe = list_entry (e, struct path_elem, elem);
    free (pe);
  }
  free (path_list);
}

static struct list*
parse_path (const char* path)
{
  struct list* path_list = malloc (sizeof (struct list));
  if (path_list == NULL)
    return NULL;
  list_init (path_list);

  char* path_copy = malloc (strlen (path) + 1);
  if (path_copy == NULL)
  {
    free (path_list);
    return NULL;
  }
  strlcpy (path_copy, path, strlen (path) + 1);

  char* token, *save_ptr;
  for (token = strtok_r (path_copy, "/", &save_ptr); token != NULL;
      token = strtok_r (NULL, "/", &save_ptr))
  {
    if (strlen (token) > NAME_MAX)
    {
      cleanup_path (path_list);
      return NULL;
    }

    struct path_elem* elem = malloc (sizeof (struct path_elem));
    if (elem == NULL)
    {
      cleanup_path (path_list);
      return NULL;
    }
    
    strlcpy (elem->name, token, strlen (token) + 1);
    list_push_back (path_list, &elem->elem);
  }

  free (path_copy);
  return path_list;
}

static struct dir*
open_path_from (struct dir* root, struct list* path_list)
{
  ASSERT (root != NULL);
  ASSERT (path_list != NULL);

  if (inode_is_removed (dir_get_inode (root)))
    return NULL;

  if (list_empty (path_list))
    return root;

  for (struct list_elem* e = list_begin (path_list);
      e != list_end (path_list); e = list_next (e))
  {
    struct path_elem* pe = list_entry (e, struct path_elem, elem);
    struct inode* inode;
    if (!dir_lookup (root, pe->name, &inode) || !inode_is_dir (inode))    
      return NULL;
   
    dir_close (root);
    root = dir_open (inode);
    if (root == NULL || inode_is_removed (inode))
      return NULL;    
  }

  return root;
}

static bool
is_root_dir (const char* path)
{
  return path[0] == '/' && strlen (path) == 1;
}

static struct dir*
open_path_root (const char* name)
{
  ASSERT (name != NULL);
  ASSERT (!is_root_dir (name));

  if (name[0] == '/' || thread_current ()->current_dir == NULL)
    return dir_open_root ();

  return dir_reopen (thread_current ()->current_dir);
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
#ifdef FS  
  if (is_root_dir (name))
    return false;
    
  struct dir *path_root = open_path_root (name);
  if (path_root == NULL)
    return false;

  struct list* path_list = parse_path (name);
  if (path_list == NULL)
  {
    dir_close (path_root);
    return false;
  }

  if (list_empty (path_list))
  {
    dir_close (path_root);
    cleanup_path (path_list);
    return false;
  }

  struct path_elem* file_elem = list_entry (list_pop_back (path_list),
      struct path_elem, elem);

  struct dir* dir = open_path_from (path_root, path_list);
  if (dir == NULL)
  {
    dir_close (path_root);
    free (file_elem);    
    cleanup_path (path_list);
    return false;
  }
#else
  struct dir *dir = dir_open_root ();
#endif
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (
#ifdef FS                    
                        INODE_FILE,
#endif                  

                        inode_sector, initial_size)

#ifdef FS                        
                  && dir_add (dir, file_elem->name, inode_sector));
#else
                  && dir_add (dir, name, inode_sector));
#endif
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
#ifdef FS  
  free (file_elem);
  cleanup_path (path_list);
#endif
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */

struct file *
filesys_open (const char *name)
{
#ifdef FS
  if (filesys_isdir (name))
    return NULL;

  struct dir *path_root = open_path_root (name);
  if (path_root == NULL)
    return NULL;

  struct list* path_list = parse_path (name);
  if (path_list == NULL)
  {
    dir_close (path_root);
    return NULL;
  }

  if (list_empty (path_list))
  {
    dir_close (path_root);
    cleanup_path (path_list);
    return false;
  }  

  struct path_elem* open_elem = list_entry (list_pop_back (path_list),
      struct path_elem, elem);
  
  struct dir* dir = open_path_from (path_root, path_list);
  if (dir == NULL)
  {
    dir_close (path_root);
    free (open_elem);
    cleanup_path (path_list);
    return NULL;
  }
#else
  struct dir *dir = dir_open_root ();
#endif  

  struct inode *inode = NULL;

#ifdef FS
  dir_lookup (dir, open_elem->name, &inode);
  free (open_elem);
  cleanup_path (path_list);
#else
  if (dir != NULL)
    dir_lookup (dir, name, &inode);
#endif

  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
#ifdef FS
  if (is_root_dir (name))
    return false;

  struct dir *path_root = open_path_root (name);
  if (path_root == NULL)
    return false;

  struct list* path_list = parse_path (name);
  if (path_list == NULL)
  {
    dir_close (path_root);
    return false;
  }

  if (list_empty (path_list))
  {
    dir_close (path_root);
    cleanup_path (path_list);
    return false;
  }  

  struct path_elem* remove_elem = list_entry (list_pop_back (path_list),
      struct path_elem, elem);

  struct dir* dir = open_path_from (path_root, path_list);
  if (dir == NULL)
  {
    dir_close (path_root);
    free (remove_elem);
    cleanup_path (path_list);
    return false;
  }
#else
  struct dir *dir = dir_open_root ();
#endif

#ifdef FS
  bool success = dir != NULL && dir_remove (dir, remove_elem->name);
  free (remove_elem);
  cleanup_path (path_list);
#else
  bool success = dir != NULL && dir_remove (dir, name);
#endif
  dir_close (dir); 

  return success;
}

#ifdef FS
struct dir*
filesys_opendir (const char *name)
{
  if (!filesys_isdir (name))
    return NULL;

  if (is_root_dir (name))
    return dir_open_root ();

  struct dir *path_root = open_path_root (name);
  if (path_root == NULL)
    return NULL;

  struct list* path_list = parse_path (name);
  if (path_list == NULL)
  {
    dir_close (path_root);
    return NULL;
  }

  struct dir* dir = open_path_from (path_root, path_list);
  if (dir == NULL)
  {
    dir_close (path_root);
    cleanup_path (path_list);
    return NULL;
  }

  return dir;
}

bool 
filesys_chdir (const char *name)
{
  if (is_root_dir (name))
    {
      if (thread_current ()->current_dir != NULL)
        dir_close (thread_current ()->current_dir);
      thread_current ()->current_dir = dir_open_root ();
      return true;
    }

  struct dir *path_root = open_path_root (name);
  if (path_root == NULL)
    return false;

  struct list* path_list = parse_path (name);
  if (path_list == NULL)
  {
    dir_close (path_root);
    return false;
  }

  struct dir* dir = open_path_from (path_root, path_list);
  if (dir == NULL)
  {
    dir_close (path_root);
    cleanup_path (path_list);
    return false;
  }

  if (thread_current ()->current_dir != NULL)
    dir_close (thread_current ()->current_dir);
  thread_current ()->current_dir = dir;
  return true;
}

bool
filesys_mkdir (const char *name)
{
  if (is_root_dir (name))
    return false;

  struct dir *path_root = open_path_root (name);
  if (path_root == NULL)
    return false;

  struct list* path_list = parse_path (name);
  if (path_list == NULL)
  {
    dir_close (path_root);
    return false;
  }

  if (list_empty (path_list))
  {
    dir_close (path_root);
    cleanup_path (path_list);
    return false;
  }  

  struct path_elem* dir_elem = list_entry (list_pop_back (path_list),
      struct path_elem, elem);

  struct dir* dir = open_path_from (path_root, path_list);
  if (dir == NULL)
  {
    dir_close (path_root);
    free (dir_elem);
    cleanup_path (path_list);
    return false;
  }

  block_sector_t inode_sector = 0;
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, 16, inode_get_inumber (dir_get_inode (dir)))
                  && dir_add (dir, dir_elem->name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  free (dir_elem);
  cleanup_path (path_list);
  dir_close (dir);

  return success;
}

static struct inode*
filesys_find (const char* name)
{
  if (is_root_dir (name))
    return dir_get_inode (dir_open_root ());

  struct dir *path_root = open_path_root (name);
  if (path_root == NULL)
    return NULL;

  struct list* path_list = parse_path (name);
  if (path_list == NULL)
  {
    dir_close (path_root);
    return NULL;
  }

  if (list_empty (path_list))
  {
    dir_close (path_root);
    cleanup_path (path_list);
    return false;
  }  

  struct path_elem* file_elem = list_entry (list_pop_back (path_list),
      struct path_elem, elem);

  struct dir* dir = open_path_from (path_root, path_list);
  if (dir == NULL)
  {
    dir_close (path_root);
    free (file_elem);
    cleanup_path (path_list);
    return NULL;
  }

  struct inode* inode;
  dir_lookup (dir, file_elem->name, &inode);
  dir_close (dir);
  free (file_elem);
  cleanup_path (path_list);
  return inode;
}

bool
filesys_exists (const char *name)
{
  struct inode* inode = filesys_find (name);
  if (inode == NULL)
    return false;
  inode_close (inode);
  return true;
}

bool
filesys_isdir (const char *name)
{
  struct inode* inode = filesys_find (name);
  if (inode == NULL)
    return false;
  bool is_dir = inode_is_dir (inode);
  inode_close (inode);
  return is_dir;
}
#endif

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16
#ifdef FS  
      , ROOT_DIR_SECTOR
#endif
    ))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
