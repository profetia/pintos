#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdbool.h>
#include <stdint.h>
#include "vm/frame.h"

#define STACK_BOTTOM ((void*) 0x08048000)

struct sup_page_table_entry {
  uint32_t* user_vaddr;
  struct frame_table_entry* frame_entry;

  uint64_t* access_time;
  bool dirty;
  bool accessed;

  struct hash_elem elem;
};

void sup_page_table_init(struct hash* sup_page_table);
void sup_page_table_destroy(struct hash* sup_page_table);

struct sup_page_table_entry* page_alloc(
    struct hash* sup_page_table, void* user_vaddr);
void page_free(struct hash* sup_page_table, 
    struct sup_page_table_entry* entry);

bool is_stack_vaddr(void* user_vaddr, void* esp);
bool is_valid_vaddr(void* user_vaddr, void* esp);

struct sup_page_table_entry* page_find(
    struct hash* sup_page_table, void* user_vaddr);

#endif