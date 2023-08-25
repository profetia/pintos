#include "vm/frame.h"
#include <debug.h>
#include <list.h>
#include <threads/synch.h>

static struct list frame_table;
static struct lock frame_table_lock;