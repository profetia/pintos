#include "vm/frame.h"
#include <debug.h>
#include <list.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

static struct list frame_table;
static struct lock frame_table_lock;

void 
frame_table_init (void) {
  list_init (&frame_table);
  lock_init (&frame_table_lock);
}

struct frame_table_entry*
frame_alloc (struct sup_page_table_entry *page_entry) 
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
      free (fte);
      return NULL;
    }

  fte->owner = thread_current ();
  fte->page_entry = page_entry;

  lock_acquire (&frame_table_lock);
  list_push_back (&frame_table, &fte->elem);
  lock_release (&frame_table_lock);

  return fte;
}

void
frame_free (struct frame_table_entry *fte) 
{
  ASSERT (fte != NULL);

  lock_acquire (&frame_table_lock);
  list_remove (&fte->elem);
  lock_release (&frame_table_lock);

  palloc_free_page (fte->frame);
  free (fte);
}
