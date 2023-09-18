#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "tanc.h"
#include "threads/thread.h"
#include "threads/malloc.h"

/* Partition that contains the file system. */
struct block *fs_device;

/* this is a function for hints. this function won't be called.*/
block_sector_t fd_to_sector(int fd){
  /* defined so. in this filesys the inode sector number is the filesystem descriptor */
  return fd;
}

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
  struct dir *dir = dir_open_root ();
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* this function is used to seek the inode of the file/dir with the given path 
  * if the path is empty, then return the cwd.
  * if the path is not empty, then we need to find the inode.
  * we return the inode no matter we find it a dir or file.
  */
struct inode * path_seek(const char * path,int cwd_fd,int * parent_fd){
  char * name = malloc(strlen(path)+1);
  strlcpy(name, path, strlen(path)+1);
  char * save_ptr;
  char * token = strtok_r(name, "/", &save_ptr);
  struct inode * inode = NULL;
  LOG_DEBUG(("path_seek: %s",path));
  if(token == NULL){
    /* if the path is empty, return the cwd */
    inode = inode_open(cwd_fd);
    if(parent_fd != NULL)
      *parent_fd = cwd_fd;
  }else{
    /* if the path is not empty, then we need to find the inode */
    struct dir * dir = dir_open(inode_open(cwd_fd));
    do{
      bool success = dir_lookup(dir, token, &inode);
      token = strtok_r(NULL, "/", &save_ptr);
      /* if we failed. it implies there is no such entry then we return NULL */
      if(!success){
        free(name);
        dir_close(dir);
        return NULL;
      }
      /* if the token is NULL, then we have reached the end of the path */
      /* if the token is not NULL, then we need to open the directory */
      if(token){    
        if(inode_is_dir(inode)){
          /* if the inode is a directory, then we open it */
          dir_close(dir);
          dir = dir_open(inode);
          if(parent_fd != NULL)
            *parent_fd = inode_get_inumber(inode);
        }
        else{
          /* if the inode is not a directory, then we return NULL */
          free(name);
          dir_close(dir);
          return NULL;
        }
      }
    }while(token != NULL);
    /* if the token is NULL, then we have reached the end of the path */
    dir_close(dir);     
  }
  free(name);
  return inode;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *_name, int cwd_fd)
{
  // struct dir *dir = dir_open(inode_open(cwd_fd));
  // if(dir == NULL)
  //   return NULL;
  // struct inode *inode = NULL;

  // if (dir != NULL)
  //   dir_lookup (dir, _name, &inode);
  // dir_close (dir);
  struct inode *inode2 = path_seek(_name, cwd_fd, NULL);
  // inode_close(inode2);

  return file_open (inode2);
  
  /* if _name is start with '/' then it is an absolute path */
  // struct inode *inode = path_seek(_name, cwd_fd);
  /* the node is already opened in path_seek */
  // return file_open(inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name, int cwd_fd) 
{
  // struct dir *dir = dir_open(inode_open(cwd_fd));
  // if(dir == NULL) return false;
  // bool success = dir != NULL && dir_remove (dir, name);
  // dir_close (dir); 
  int parent_fd;
  struct inode *inode = path_seek(name, cwd_fd, &parent_fd);
  if(inode == NULL)
    return false;
  if(inode_is_dir(inode)){
    /* if the inode is a directory, then we need to check whether it is empty */
    struct dir * dir = dir_open(inode);
    if(!dir_is_empty(dir)){
      /* if the directory is not empty, then we return false */
      inode_close(inode);
      dir_close(dir);
      return false;
    }
  }
  if(parent_fd == NOT_A_FD){
    inode_close(inode);
    return false;
  }
  struct dir * dir = dir_open(inode_open(parent_fd));
  if(dir == NULL){
    inode_close(inode);
    return false;
  }
  bool success = dir_remove(dir, name);
  inode_close(inode);
  dir_close(dir);
  return success;
  // return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
