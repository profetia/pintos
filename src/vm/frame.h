#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>

struct frame_table_entry {
    uint32_t *frame;
    struct thread *owner;
    struct sup_page_entry *aux;

    struct list_elem elem;
};


#endif // VM_FRAME_H