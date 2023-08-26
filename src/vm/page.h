#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdbool.h>
#include <stdint.h>
#include "filesys/file.h"
#include "vm/frame.h"

#define STACK_BOTTOM ((void*) 0x08048000)

enum page_location {
  PAGE_LOC_SWAP,
  PAGE_LOC_FILESYS,
  PAGE_LOC_MEMORY,
  PAGE_LOC_ERROR
};

struct sup_page_table_entry {
  uint32_t* user_vaddr;

  enum page_location location;

  struct frame_table_entry* frame_entry;
  size_t swap_index;
  struct file* file;
  off_t file_offset;

  bool writable;

  struct hash_elem elem;
};

void sup_page_table_init(struct hash* sup_page_table);
void sup_page_table_destroy(struct hash* sup_page_table);

struct sup_page_table_entry* page_alloc(
    struct hash* sup_page_table, const void* user_vaddr, bool writable);
void page_free(struct hash* sup_page_table, 
    struct sup_page_table_entry* entry);

bool is_stack_vaddr(const void* esp, const void* user_vaddr);

struct sup_page_table_entry* page_find(
    struct hash* sup_page_table, const void* user_vaddr);

bool page_pull (const void* user_addr);

#endif