#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stdint.h>

struct sup_page_table_entry {
  uint32_t* user_vaddr;

  uint64_t* access_time;
  bool dirty;
  bool accessed;
};

#endif