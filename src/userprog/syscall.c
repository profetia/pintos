#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <tanc.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);

static void syscall_halt (void);
static void syscall_exit (int status);
static tid_t syscall_exec (const char *cmd_line);
static int syscall_wait (tid_t pid);
static bool syscall_create (const char *file, off_t initial_size);
static bool syscall_remove (const char *file);
static int syscall_open (const char *file);
static int syscall_filesize (int fd);
static int syscall_read (int fd, void *buffer, unsigned size);
static int syscall_write (int fd, const void *buffer, unsigned size);
static void syscall_seek (int fd, unsigned position);
static unsigned syscall_tell (int fd);
static void syscall_close (int fd);

static bool is_valid_vaddr (const void *vaddr);
static bool is_valid_vrange (const void *vaddr, unsigned size);
static bool is_valid_word (const void *vaddr);
static bool is_valid_string (const char *str);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  if (!is_valid_word(f->esp)) {
    syscall_exit(-1);
  }

  int sys_code = *(int *)f->esp;

  switch (sys_code)
    {
      case SYS_HALT:
        syscall_halt ();
        break;
      case SYS_EXIT:
        if (!is_valid_word(f->esp + 4))       
          syscall_exit(-1);      
        syscall_exit(*(int *)(f->esp + 4));
        break;
      case SYS_EXEC:
        if (!is_valid_word(f->esp + 4))
          syscall_exit(-1);        
        f->eax = syscall_exec(*(char **)(f->esp + 4));
        break;
      case SYS_WAIT:
        if (!is_valid_word(f->esp + 4))
          syscall_exit(-1);        
        f->eax = syscall_wait(*(tid_t *)(f->esp + 4));
        break;
      case SYS_CREATE:
        if (!is_valid_word(f->esp + 4) || !is_valid_word(f->esp + 8)) 
          syscall_exit(-1);        
        f->eax = syscall_create(*(char **)(f->esp + 4), *(off_t *)(f->esp + 8));
        break;
      case SYS_REMOVE:
        if (!is_valid_word(f->esp + 4))
          syscall_exit(-1);        
        f->eax = syscall_remove(*(char **)(f->esp + 4));
        break;
      case SYS_OPEN:
        if (!is_valid_word(f->esp + 4))
          syscall_exit(-1);        
        f->eax = syscall_open(*(char **)(f->esp + 4));
        break;
      case SYS_FILESIZE:
        if (!is_valid_word(f->esp + 4))
          syscall_exit(-1);        
        f->eax = syscall_filesize(*(int *)(f->esp + 4));
        break;
      case SYS_READ:
        if (!is_valid_word(f->esp + 4) || !is_valid_word(f->esp + 8) 
            || !is_valid_word(f->esp + 12)) 
          syscall_exit(-1);        
        f->eax = syscall_read(*(int *)(f->esp + 4), *(void **)(f->esp + 8),
                             *(unsigned *)(f->esp + 12));
        break;
      case SYS_WRITE:
        if (!is_valid_word(f->esp + 4) || !is_valid_word(f->esp + 8) 
            || !is_valid_word(f->esp + 12)) 
          syscall_exit(-1);        
        f->eax = syscall_write(*(int *)(f->esp + 4), *(void **)(f->esp + 8),
                              *(unsigned *)(f->esp + 12));
        break;
      case SYS_SEEK:
        if (!is_valid_word(f->esp + 4) || !is_valid_word(f->esp + 8)) 
          syscall_exit(-1);        
        syscall_seek(*(int *)(f->esp + 4), *(unsigned *)(f->esp + 8));
        break;
      case SYS_TELL:
        if (!is_valid_word(f->esp + 4))
          syscall_exit(-1);        
        f->eax = syscall_tell(*(int *)(f->esp + 4));
        break;
      case SYS_CLOSE:
        if (!is_valid_word(f->esp + 4))
          syscall_exit(-1);        
        syscall_close(*(int *)(f->esp + 4));
        break;
      default:
        syscall_exit(-1);
    }
}

static void 
syscall_halt (void) 
{
  shutdown_power_off();
}

static void
syscall_exit (int status)
{
  thread_current()->exit_status = status;
  thread_exit();
}

static tid_t
syscall_exec (const char *cmd_line)
{
  if (!is_valid_string(cmd_line)) 
    syscall_exit(-1);
  return process_execute(cmd_line);
}

static int
syscall_wait (tid_t pid)
{
  return process_wait(pid);
}

static bool
syscall_create (const char *file, off_t initial_size)
{
  if (!is_valid_string(file)) 
    syscall_exit (-1);
  lock_acquire (&fs_lock);
  bool success = filesys_create(file, initial_size);
  lock_release (&fs_lock);
  return success;
}

static bool
syscall_remove (const char *file)
{
  if (!is_valid_string(file)) 
    syscall_exit (-1);
  lock_acquire (&fs_lock);
  bool success = filesys_remove(file);
  lock_release (&fs_lock);
  return success;
}

static int
syscall_open (const char *file)
{
  if (!is_valid_string(file)) 
    syscall_exit (-1);
  lock_acquire (&fs_lock);
  struct file *f = filesys_open(file);
  lock_release (&fs_lock);
  if (f == NULL) {
    return -1;
  }
  return process_add_file(f);
}

static int
syscall_filesize (int fd)
{
  struct file *f = process_get_file(fd);
  if (f == NULL) {
    syscall_exit(-1);
  }
  lock_acquire (&fs_lock);
  int size = file_length(f);
  lock_release (&fs_lock);
  return size;
}

static int
syscall_read (int fd, void *buffer, unsigned size)
{
  if (size == 0) 
    return 0;

  if (!is_valid_vrange(buffer, size)) 
    syscall_exit(-1);

  if (fd == STDIN_FILENO) 
    {      
      uint8_t *buf = (uint8_t *)buffer;
      for (unsigned i = 0; i < size; i++) {
        buf[i] = input_getc();
      }
      return (int)size;
    } 

  struct file *f = process_get_file(fd);
  if (f == NULL) 
    syscall_exit(-1);
  
  lock_acquire (&fs_lock);
  int bytes_read = file_read(f, buffer, (off_t)size);
  lock_release (&fs_lock);
  return bytes_read;
}

static int
syscall_write (int fd, const void *buffer, unsigned size)
{
  if (size == 0) 
    return 0;

  if (!is_valid_vrange(buffer, size)) 
    syscall_exit(-1);

  if (fd == STDOUT_FILENO) 
    {
      putbuf(buffer, size);
      return (int)size;
    }

  struct file *f = process_get_file(fd);
  if (f == NULL) 
    syscall_exit(-1);
  
  lock_acquire (&fs_lock);
  int bytes_written = file_write(f, buffer, (off_t)size);
  lock_release (&fs_lock);
  return bytes_written;
}

static void
syscall_seek (int fd, unsigned position)
{
  struct file *f = process_get_file(fd);
  if (f == NULL) 
    syscall_exit(-1);
  lock_acquire (&fs_lock);
  file_seek(f, (off_t)position);
  lock_release (&fs_lock);
}

static unsigned
syscall_tell (int fd)
{
  struct file *f = process_get_file(fd);
  if (f == NULL) 
    syscall_exit(-1);
  lock_acquire (&fs_lock);
  unsigned position = file_tell(f);
  lock_release (&fs_lock);
  return position;
}

static void
syscall_close (int fd)
{
  struct file *f = process_get_file(fd);
  if (f == NULL) 
    syscall_exit(-1);
  process_close_file(fd);
}


/* Returns true if the given virtual address is valid,
   which is to say that it is not NULL, it is in user
   memory, and it is mapped to a page in the current
   process's page table. Returns false otherwise. */
static bool 
is_valid_vaddr (const void *vaddr) 
{
  return vaddr != NULL
      && is_user_vaddr(vaddr) 
      && pagedir_get_page(thread_current()->pagedir, vaddr) != NULL;
}

/* Returns true if the given virtual address range is valid,
   false otherwise. */
static bool 
is_valid_vrange (const void *vaddr, unsigned size) 
{
  const void *end = vaddr + size - 1;
  if (end < vaddr) {
    return false;
  }
  return is_valid_vaddr(vaddr) && is_valid_vaddr(end);
}

static bool 
is_valid_word (const void *vaddr)
{
  return is_valid_vrange(vaddr, sizeof(int));
}

static bool
is_valid_byte (const void *vaddr)
{
  return is_valid_vaddr(vaddr);
}


/* Returns true if the given string is valid, false 
   otherwise. */
static bool
is_valid_string (const char *str)
{
  while (is_valid_vaddr(str)) {
    if (*str == '\0') {
      return true;
    }
    str++;
  }
  return false;
}