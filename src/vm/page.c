#include "vm/page.h"
#include <debug.h>
#include <string.h>
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
page_alloc(struct hash* sup_page_table, const void* user_vaddr) {
  ASSERT(sup_page_table != NULL);
  ASSERT(user_vaddr != NULL);

  struct sup_page_table_entry* entry = malloc(
      sizeof(struct sup_page_table_entry));
  if (entry == NULL) {
    return NULL;
  }

  entry->frame_entry = frame_alloc(entry);
  if (entry->frame_entry == NULL) {
    free(entry);
    return NULL;
  }

  entry->user_vaddr = pg_round_down(user_vaddr);
  entry->access_time = NULL;
  entry->dirty = false;
  entry->accessed = false;

  struct hash_elem* old_elem = hash_insert(sup_page_table, &entry->elem);
  if (old_elem != NULL) {
    frame_free(entry->frame_entry);
    free(entry);
    return NULL;
  }

  return entry;
}

void 
page_free(struct hash* sup_page_table, struct sup_page_table_entry* entry)
{
  ASSERT(sup_page_table != NULL);
  ASSERT(entry != NULL);

  hash_delete(sup_page_table, &entry->elem);
  frame_free(entry->frame_entry);
  free(entry);
}

bool
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
page_pull (const void* user_addr)
{
  ASSERT (user_addr != NULL);

    struct sup_page_table_entry *spte = page_find(
    &thread_current()->sup_page_table, user_addr);
  
  if (spte == NULL) 
    {
      spte = page_alloc(&thread_current()->sup_page_table, user_addr);
      if (spte == NULL) 
        {
          return false;
        }
    }

  if (!install_page(spte->user_vaddr, spte->frame_entry->frame, true)) 
    {
      if (spte != NULL) 
        {
          page_free(&thread_current()->sup_page_table, spte);
        }
      return false;
    }

  return true;
}