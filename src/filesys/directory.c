#include "filesys/directory.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "stdbool.h"
#include "threads/malloc.h"
#include "tanc.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt, block_sector_t parent_sector)
{
  bool success = inode_create (sector,  (int) (entry_cnt * sizeof (struct dir_entry)) ,true);
  if(success){
    /* add . */
    struct inode *inode = inode_open(sector);
    if(inode == NULL){
      return false;
    }
    struct dir_entry * e = calloc(1,sizeof(struct dir_entry));
    if(e == NULL){
      inode_close(inode);
      return false;
    }
    e->inode_sector = sector;
    e->in_use = true;
    strlcpy(e->name,".",sizeof(e->name));
    inode_write_at(inode,e,sizeof(struct dir_entry),0);
    /* add .. */
    e->inode_sector = parent_sector;
    strlcpy(e->name,"..",sizeof(e->name));
    inode_write_at(inode,e,sizeof(struct dir_entry),sizeof(struct dir_entry));
    
  }
  return success;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME. NAME shouldn't be a path.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL)){
    // LOG_DEBUG(("e.inode_sector = %u",e.inode_sector));
    *inode = inode_open (e.inode_sector);
    // LOG_DEBUG(("inode mem loc=%u, sector=%u",*inode, e.inode_sector));
  }
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs)){
    LOG_DEBUG(("dir_remove: lookup failed"));
    goto done;
  }

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL){
    LOG_DEBUG(("dir_remove: inode_open failed"));
    goto done;
  }
  /* If the inode represents a dir*/
  if(inode_is_dir(inode)){
    struct dir * dir_local = dir_open(inode);
    if(!dir_is_empty(dir_local)){
      dir_close(dir_local);
      return false;
    }
    free(dir_local);
  }
  /* Remove inode. */
  inode_remove (inode);
  inode_close (inode);

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) {
    LOG_DEBUG(("dir_remove: inode_write_at failed"));
    goto done;
  }
  success = true;

 done:
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          // do not return . and ..
          if(strcmp(e.name,".") == 0 || strcmp(e.name,"..") == 0)
            continue;
          strlcpy (name, e.name, NAME_MAX + 1);
          // LOG_DEBUG(("dir_readdir: %s, inumber=%u",name, inode_get_inumber(dir->inode)));
          return true;
        } 
    }
  return false;
}

bool dir_is_empty(struct dir * dir){
  struct dir_entry e;
  off_t ofs;
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e){
    if(e.in_use && strcmp(e.name,".") != 0 && strcmp(e.name,"..") != 0){
      return false;
    }
  }
  return true;
}

/* this function is used to seek the inode of the file/dir with the given path 
  * If the path exists. return the inode of the file/dir. Set the parent_fd to the fd of the parent directory.
  * If the path doest not exist
    * if the path is empty, then return NULL and set the parent_fd to the NOT_A_FD
    * if the parent directory exists, then return NULL and set the parent_fd to the fd of the parent directory.
    * if the parent directory does not exist, then return NULL and set the parent_fd to NOT_A_FD
*/
struct inode * path_seek(const char * path,int cwd_fd,int* parent_fd){
  /*if is empty path, return NULL and set the parent */
  if(!strcmp(path,"")){
    if(parent_fd != NULL)
      *parent_fd = NOT_A_FD;
    return NULL;
  }
  if(parent_fd != NULL)
      *parent_fd = NOT_A_FD;
  
  char * name = malloc(strlen(path)+1);
  strlcpy(name, path, strlen(path)+1);
  char * save_ptr = name;
  char * token = strtok_r(name, "/", &save_ptr);
  struct inode * inode = NULL;

  // LOG_DEBUG(("path_seek: %s first token is %s",path,token));
  /*if path starts with '/', chang the cwd_fd to root fd */
  int save_cwd_fd = cwd_fd;
  if(path[0] == '/'){
    cwd_fd = ROOT_DIR_FD;
    save_cwd_fd = ROOT_DIR_FD;
  }
  /*if the token is none, it must be '///...' */
  if(token == NULL || !strcmp(token,"")){
    free(name);
    if(parent_fd != NULL)
      *parent_fd = cwd_fd;
    return inode_open(ROOT_DIR_FD);
  }
  /* first check how many tokens are there*/
  int token_len = 0;
  while(token){
    token = strtok_r(NULL, "/", &save_ptr);
    ++token_len;
  }
  // LOG_DEBUG(("%d tokens found",token_len));
  strlcpy(name, path, strlen(path)+1);
  save_ptr = name;
  token = strtok_r(name, "/", &save_ptr);
  inode = NULL;

  int token_id = 0;
  while(token){
    // LOG_DEBUG(("token %d is %s",token_id,token));
    inode = NULL;
    struct inode * pa = inode_open(cwd_fd);
    if(inode_is_removed(pa)){
      free(name);
      // LOG_DEBUG(("parent not exist (is removed)"));
      return false;
    }
    struct dir * dir = dir_open(pa);
    //check whether the token exist in the dir
    bool success = dir_lookup(dir,token,&inode);
    if(token_id == token_len - 1){
      if(parent_fd != NULL)
        *parent_fd = cwd_fd;
      // LOG_DEBUG(("parent exist %d and target might exist %d (mem loc=%u).And the success_dirlookup = %d",
      //            parent_fd != NULL ? *parent_fd : -1,inode ? inode_get_inumber(inode) : -1, inode, success
      //           ));      
      dir_close(dir);
      free(name);
      // LOG_DEBUG(("parent exist %d and target might exist %d (mem loc=%u).And the success_dirlookup = %d",
      //            parent_fd != NULL ? *parent_fd : -1,inode ? inode_get_inumber(inode) : -1, inode, success
      //           ));
      return inode;
    }    
    if(!success){ 
      if(token_id == token_len - 2){
        //then the token is the last token. 
        if(parent_fd != NULL)
          *parent_fd = cwd_fd;
        dir_close(dir);
        // inode_close(pa);
        free(name);
        // LOG_DEBUG(("parent exist but target not"));
        return NULL;
      }//otherwise the path is not valid.
      dir_close(dir);
      // inode_close(pa);
      free(name);
      if(parent_fd != NULL)
        *parent_fd = cwd_fd;
      // LOG_DEBUG(("parent not exist"));
      return NULL;
    }//otherwise the token is found in the dir
    //if the next token is the last token, then return the inode
    if(token_id == token_len - 1){
      dir_close(dir);
      // inode_close(pa);
      free(name);
      if(parent_fd != NULL)
        *parent_fd = cwd_fd;
      // LOG_DEBUG(("parent exist and target exist, success=%d, inode=%u",success,inode));;
      return inode;
    }
    //otherwise open it and continue
    dir_close(dir);
    // inode_close(pa);
    if(inode_is_removed(inode)){
      free(name);
      // LOG_DEBUG(("parent not exist (is removed)"));
      return NULL;
    }
    if(inode_is_dir(inode)){
      cwd_fd = (int) inode_get_inumber(inode);
    }else{
      inode_close(inode);
      free(name);
      // LOG_DEBUG(("parent not exist (is a file)"));
      return NULL;
    }
    inode_close(inode);
    ++token_id;
    token = strtok_r(NULL, "/", &save_ptr);
  }
  /*impossible case*/
  free(name);
  return NULL;
}

bool get_last_token(char * path,char ** last_token){
  char * name = malloc(strlen(path)+1);
  strlcpy(name, path, strlen(path)+1);
  char * save_ptr;
  char * token = strtok_r(name, "/", &save_ptr);
  char * last = NULL;
  while(token){
    last = token;
    token = strtok_r(NULL, "/", &save_ptr);
  }
  if(last == NULL){
    free(name);
    return false;
  }
  *last_token = (last - name) + path;
  free(name);
  return true;
}

int dfs(const char* name, struct inode * inode){
  LOG_DEBUG(("%s %u",name,inode_get_inumber(inode)));
  
  return 1;
}

static void transverse(struct inode* current, int depth){
  if(current == NULL)
    return;
  struct dir * dir = dir_open(current);
  if(dir == NULL)
    return;
  struct dir_entry e;
  off_t ofs;
  for (ofs = 0; inode_read_at (current, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e){
    if(e.in_use && strcmp(e.name,".") != 0 && strcmp(e.name,"..") != 0){
      struct inode * inode = inode_open(e.inode_sector);
      if(inode == NULL){
        LOG_DEBUG(("inode_open failed, sector %d with name= %s",e.inode_sector,e.name));
      }
      if(inode_is_dir(inode)){
        for(int i = 0; i < depth; ++i)
          printf("  ");
        printf("|-%s sec=%u\n",e.name,e.inode_sector);
        transverse(inode,depth+1);
      }else{
        for(int i = 0; i < depth; ++i)
          printf("  ");
        printf("|-%s sec=%u\n",e.name,e.inode_sector);
      }
      inode_close(inode);
    }else if(e.in_use){
      for(int i = 0; i < depth; ++i)
        printf("  ");
      printf("|-%s sec=%u\n",e.name,e.inode_sector);
    }
  }
  dir_close(dir);
}

//print a tree from the root
void print_tree() {
  struct dir * dir = dir_open_root();
  printf("/\n");
  transverse(dir_get_inode(dir),0);
  dir_close(dir);
}