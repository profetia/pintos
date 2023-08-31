#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/synch.h"
#include "threads/thread.h"

#ifdef FS
#include "filesys/inode.h"
#endif

#ifndef FS
extern struct lock fs_lock;
#endif

void process_init (void);
tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

bool install_page (void *upage, void *kpage, bool writable);

struct file_elem
  {
#ifdef FS
    enum inode_type type;
    void* file;
#else
    struct file* file;
#endif
    int fd;
    struct list_elem elem;
  }; 

#ifdef FS
int process_add_file (enum inode_type type, void *f);
#else
int process_add_file (struct file *f);
#endif

#ifdef FS
struct file_elem *process_get_file (int fd);
#else
struct file *process_get_file (int fd);
#endif
void process_close_file (int fd);

#ifdef VM
struct mmap_file
  {
    int mapid;
    void* user_addr;
    struct file *file;
    size_t num_pages;
    struct list_elem elem;
  };

int process_add_mmap (struct file *f, void *addr);
struct mmap_file *process_get_mmap (int mapid);
void process_remove_mmap (int mapid);
#endif

#endif /* userprog/process.h */
