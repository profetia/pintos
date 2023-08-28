#include "vm/frame.h"
#include <debug.h>
#include <list.h>
#include <tanc.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/swap.h"

static struct list frame_table;
static struct lock frame_table_lock;

static void frame_evict (void);
static struct frame_table_entry* frame_find_victim (void);

void 
frame_table_init (void) {
  list_init (&frame_table);
  lock_init (&frame_table_lock);
}

struct frame_table_entry*
frame_alloc (struct sup_page_table_entry *page_entry, 
    uint32_t* user_vaddr, bool writable) 
{
  ASSERT (page_entry != NULL);

  struct frame_table_entry *fte = malloc (sizeof (struct frame_table_entry));
  if (fte == NULL) 
    {
      return NULL;
    }

  fte->frame = palloc_get_page (PAL_USER | PAL_ZERO);
  if (fte->frame == NULL) 
    {
      frame_evict ();
      fte->frame = palloc_get_page (PAL_USER | PAL_ZERO);
      ASSERT (fte->frame != NULL);
    }

  fte->owner = thread_current ();
  fte->page_entry = page_entry;

  if (!install_page (user_vaddr, fte->frame, writable)) 
    {
      palloc_free_page (fte->frame);
      free (fte);
      return NULL;
    }

  lock_acquire (&frame_table_lock);
  list_push_back (&frame_table, &fte->elem);
  lock_release (&frame_table_lock);

  return fte;
}

void
frame_free (struct frame_table_entry *fte) 
{
  ASSERT (fte != NULL);

  pagedir_clear_page (fte->owner->pagedir, fte->page_entry->user_vaddr);

  lock_acquire (&frame_table_lock);
  list_remove (&fte->elem);
  lock_release (&frame_table_lock);

  palloc_free_page (fte->frame);
  free (fte);
}

static void
frame_evict (void) 
{
  struct frame_table_entry *fte = frame_find_victim ();
  ASSERT (fte != NULL);
  ASSERT (fte->page_entry != NULL);
  ASSERT (fte->page_entry->location == PAGE_LOC_MEMORY);

  lock_acquire (fte->page_entry->lock);
  if (fte->page_entry->location == PAGE_LOC_MEMORY)
    {
      size_t index = swap_evict ((uint8_t*)fte->frame);
      fte->page_entry->location = PAGE_LOC_SWAP;
      fte->page_entry->swap_index = index;
      fte->page_entry->frame_entry = NULL;
    }
  else if (fte->page_entry->location == PAGE_LOC_MMAPPED)
    {
      lock_acquire (&fs_lock);
      file_write_at (fte->page_entry->file, fte->frame, 
          fte->page_entry->read_bytes, fte->page_entry->file_offset);
      lock_release (&fs_lock);

      fte->page_entry->location = PAGE_LOC_FILESYS;  
      fte->page_entry->frame_entry = NULL;    
    }
  else
    NOT_REACHED ();

  pagedir_clear_page (fte->owner->pagedir, fte->page_entry->user_vaddr);
  palloc_free_page (fte->frame);
  lock_release (fte->page_entry->lock);
  free (fte);    
}

static struct frame_table_entry*
frame_find_victim (void) 
{
  struct list_elem *e;
  struct frame_table_entry *fte;

  lock_acquire (&frame_table_lock);
  e = list_pop_front (&frame_table);
  fte = list_entry (e, struct frame_table_entry, elem);
  lock_release (&frame_table_lock);

  return fte;
}
