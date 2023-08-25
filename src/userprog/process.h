#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/synch.h"
#include "threads/thread.h"

static struct lock fs_lock;

void process_init (void);
tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

bool install_page (void *upage, void *kpage, bool writable);

int process_add_file (struct file *f);
struct file *process_get_file (int fd);
void process_close_file (int fd);

#endif /* userprog/process.h */
