#include "vm/page.h"
#include <debug.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"

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
page_alloc(struct hash* sup_page_table, void* user_vaddr) {
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
is_stack_vaddr(void* user_vaddr, void* esp) {
  ASSERT(esp != NULL);

  return user_vaddr != NULL && user_vaddr >= STACK_BOTTOM && 
      user_vaddr >= esp - 32;
}

bool
is_valid_vaddr(void* user_vaddr, void* esp) {
  ASSERT(esp != NULL);

  return user_vaddr != NULL && is_user_vaddr(user_vaddr) && is_stack_vaddr(
      user_vaddr, esp);
}

struct sup_page_table_entry*
page_find(struct hash* sup_page_table, void* user_vaddr) {
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