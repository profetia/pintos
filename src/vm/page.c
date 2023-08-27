#include "vm/page.h"
#include <debug.h>
#include <string.h>
#include "filesys/file.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

static unsigned 
sup_page_hash(const struct hash_elem* e, void* aux UNUSED) 
{
  struct sup_page_table_entry* entry = hash_entry(
      e, struct sup_page_table_entry, elem);
  return hash_bytes(&entry->user_vaddr, sizeof(entry->user_vaddr));
}

static bool
sup_page_less(const struct hash_elem* a, const struct hash_elem* b,
    void* aux UNUSED) 
{
  struct sup_page_table_entry* entry_a = hash_entry(
      a, struct sup_page_table_entry, elem);
  struct sup_page_table_entry* entry_b = hash_entry(
      b, struct sup_page_table_entry, elem);
  return entry_a->user_vaddr < entry_b->user_vaddr;
}

void 
sup_page_table_init(struct hash* sup_page_table) {
  hash_init(sup_page_table, sup_page_hash, sup_page_less, NULL);
}

void
sup_page_table_destroy(struct hash* sup_page_table) {
  hash_destroy(sup_page_table, NULL);
}

struct sup_page_table_entry*
page_create(struct hash* sup_page_table, const void* user_vaddr, 
    enum page_location location, struct frame_table_entry* frame_entry, 
    size_t swap_index, struct file* file, off_t file_offset, 
    off_t read_bytes, off_t zero_bytes, bool writable) 
{
  ASSERT(user_vaddr != NULL);

  struct sup_page_table_entry* entry = malloc(
      sizeof(struct sup_page_table_entry));
  if (entry == NULL) {
    return NULL;
  }

  entry->user_vaddr = pg_round_down(user_vaddr);
  entry->location = location;
  entry->frame_entry = frame_entry;
  entry->swap_index = swap_index;
  entry->file = file;
  entry->file_offset = file_offset;
  entry->read_bytes = read_bytes;
  entry->zero_bytes = zero_bytes;
  entry->writable = writable;
  
  struct hash_elem* old_elem = hash_insert(sup_page_table, &entry->elem);
  if (old_elem != NULL) {
    free(entry);
    return NULL;
  }

  return entry;
}

static struct sup_page_table_entry* page_zero(struct sup_page_table_entry *spte);
static struct sup_page_table_entry* page_reclaim (struct sup_page_table_entry *spte);
static struct sup_page_table_entry* page_map (struct sup_page_table_entry *spte);
static void page_unmap (struct sup_page_table_entry *spte);

void 
page_destroy(struct hash* sup_page_table, struct sup_page_table_entry* entry)
{
  ASSERT(sup_page_table != NULL);
  ASSERT(entry != NULL);

  switch (entry->location) {
    case PAGE_LOC_ZERO:
      // Do nothing. The page is all zeros.
      break;
    case PAGE_LOC_SWAP:
      swap_free(entry->swap_index);
      break;
    case PAGE_LOC_FILESYS:
      // Do nothing. The page is not responsible for closing the file.
      break;
    case PAGE_LOC_MEMORY:
      frame_free(entry->frame_entry);
      break;
    case PAGE_LOC_MMAPPED:
      page_unmap(entry);
      break;
    default:
      NOT_REACHED();
  }

  hash_delete(sup_page_table, &entry->elem);
  free(entry);
}

struct sup_page_table_entry*
page_alloc(struct hash* sup_page_table, const void* user_vaddr, bool writable) 
{
  ASSERT(sup_page_table != NULL);
  ASSERT(user_vaddr != NULL);

  struct sup_page_table_entry* entry = page_create(
      sup_page_table, user_vaddr, PAGE_LOC_MEMORY,
      NULL, BITMAP_ERROR, NULL, 0, 0, 0, writable);
  if (entry == NULL) {
    return NULL;
  }

  entry->frame_entry = frame_alloc(entry, pg_round_down(user_vaddr), writable);
  if (entry->frame_entry == NULL) {
    free(entry);
    return NULL;
  }

  return entry;
}

struct sup_page_table_entry *
page_mmap (struct hash *sup_page_table, struct file *file, off_t offset,
           const uint32_t *user_vaddr, uint32_t read_bytes,
           uint32_t zero_bytes, bool writable)
{
  ASSERT (sup_page_table != NULL);
  ASSERT (file != NULL);
  ASSERT (user_vaddr != NULL);
  ASSERT (read_bytes + zero_bytes > 0);

  struct sup_page_table_entry *spte = page_create(
      sup_page_table, user_vaddr, PAGE_LOC_FILESYS, NULL, BITMAP_ERROR, 
      file, offset, (off_t)read_bytes, (off_t)zero_bytes, writable);

  return spte;
}

static bool
is_stack_vaddr(const void* esp, const void* user_vaddr) {
  ASSERT(esp != NULL);

  return user_vaddr >= STACK_BOTTOM && 
      user_vaddr >= esp - 32;
}

struct sup_page_table_entry*
page_find(struct hash* sup_page_table, const void* user_vaddr) {
  ASSERT(sup_page_table != NULL);
  ASSERT(user_vaddr != NULL);

  struct sup_page_table_entry entry;
  entry.user_vaddr = pg_round_down(user_vaddr);

  struct hash_elem* elem = hash_find(sup_page_table, &entry.elem);
  if (elem == NULL) {
    return NULL;
  }

  return hash_entry(elem, struct sup_page_table_entry, elem);
}

bool
page_overlaps(struct hash* sup_page_table, const void* user_vaddr, size_t size) 
{
  ASSERT(sup_page_table != NULL);
  ASSERT(user_vaddr != NULL);

  const void* end = user_vaddr + size;
  const void* page = pg_round_down(user_vaddr);
  while (page < end) {
    if (page_find(sup_page_table, page) != NULL) {
      return true;
    }
    page += PGSIZE;
  }

  return false;
}

struct sup_page_table_entry*
page_pull (struct hash* sup_page_table, const void* esp, 
    const void* user_addr, bool write)
{
  ASSERT (esp != NULL);
  ASSERT (user_addr != NULL);

  struct sup_page_table_entry *spte = page_find(sup_page_table, user_addr);

  if (spte == NULL) 
    {
      if (!is_stack_vaddr(esp, user_addr)) return false;        
      spte = page_alloc(sup_page_table, user_addr, true);
      return spte;
    }

  if (write && !spte->writable) return NULL;

  switch (spte->location)
    {
      case PAGE_LOC_ZERO:
        return page_zero(spte);
      case PAGE_LOC_MEMORY:
        return spte;
      case PAGE_LOC_SWAP:
        return page_reclaim(spte);
      case PAGE_LOC_FILESYS:
        return page_map(spte);
      case PAGE_LOC_MMAPPED:
        return spte;
      default:
        return NULL;        
    }       
}

static struct sup_page_table_entry*
page_zero(struct sup_page_table_entry *spte) 
{
  ASSERT (spte != NULL);
  ASSERT (spte->location == PAGE_LOC_ZERO);

  struct frame_table_entry *fte = frame_alloc(
      spte, spte->user_vaddr, spte->writable);
  if (fte == NULL) return NULL;

  spte->frame_entry = fte;
  spte->location = PAGE_LOC_MEMORY;

  return spte;
}

static struct sup_page_table_entry*
page_reclaim (struct sup_page_table_entry *spte) 
{
  ASSERT (spte != NULL);
  ASSERT (spte->location == PAGE_LOC_SWAP);

  struct frame_table_entry *fte = frame_alloc(
      spte, spte->user_vaddr, spte->writable);
  if (fte == NULL) return NULL;

  swap_reclaim((uint8_t*)fte->frame, spte->swap_index);
  spte->frame_entry = fte;
  spte->location = PAGE_LOC_MEMORY;
  spte->swap_index = BITMAP_ERROR;

  return spte;
}

static struct sup_page_table_entry*
page_map (struct sup_page_table_entry *spte) 
{
  ASSERT (spte != NULL);
  ASSERT (spte->location == PAGE_LOC_FILESYS);

  struct frame_table_entry *fte = frame_alloc(
      spte, spte->user_vaddr, spte->writable);
  if (fte == NULL) return NULL;

  lock_acquire (&fs_lock);  
  if (file_read_at (spte->file, fte->frame, spte->read_bytes, 
      spte->file_offset) != (int)spte->read_bytes)
    {
      frame_free (fte);
      lock_release (&fs_lock);
      return NULL;
    }
  memset (fte + spte->read_bytes, 0, spte->zero_bytes);
  lock_release (&fs_lock);

  spte->frame_entry = fte;
  spte->location = PAGE_LOC_MMAPPED;

  return spte;
}

static void 
page_unmap (struct sup_page_table_entry *spte)
{
  ASSERT (spte != NULL);
  ASSERT (spte->location == PAGE_LOC_MMAPPED);

  struct frame_table_entry *fte = spte->frame_entry;
  ASSERT (fte != NULL);

  lock_acquire (&fs_lock);
  file_write_at (spte->file, fte->frame, spte->read_bytes, spte->file_offset);
  lock_release (&fs_lock);

  frame_free (fte);
  spte->frame_entry = NULL;
  spte->location = PAGE_LOC_FILESYS;
}