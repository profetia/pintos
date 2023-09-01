             +-------------------------+
             |         CS 140          |
             | PROJECT 4: FILE SYSTEMS |
             |     DESIGN DOCUMENT     |
             +-------------------------+

#### GROUP 

> Fill in the names and email addresses of your group members.

Linshu Yang <yanglsh@shanghaitech.edu.cn>

Lei Huang <huanglei@shanghaitech.edu.cn>

#### PRELIMINARIES 

> If you have any preliminary comments on your submission, notes for the
> TAs, or extra credit, please give them here.

> Please cite any offline or online sources you consulted while
> preparing your submission, other than the Pintos documentation, course
> text, lecture notes, and course staff.

             INDEXED AND EXTENSIBLE FILES
             ============================

#### DATA STRUCTURES 

> A1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

```c
// src/filesys/inode.h

...

struct inode_disk
  {
    ...
    block_sector_t blocks[NUM_BLOCKS];  /* Data blocks. */
    // The first NUM_DIRECT_POINTERS blocks are direct blocks.
    // The next NUM_INDIRECT_BLOCKS blocks are indirect blocks.
    // The last NUM_DOUBLE_INDIRECT_BLOCKS blocks are double indirect blocks.
    enum inode_type type;               /* File or directory. */
    ...
  };

struct inode
    {
        ...
        struct lock lock;               /* Inode lock. */
        ...
    }

...

// src/filesys/inode.c

...

struct indirect_block
  {
    block_sector_t blocks[BLOCK_SECTOR_SIZE / sizeof (block_sector_t)];
  };

...

```

- In `struct inode_disk`:
    - `blocks`: The data blocks of the inode.
    
- In `struct inode`:
    - `lock`: The lock of the inode.

- `struct indirect_block`: The indirect block.
    - `blocks`: The data blocks of the indirect block.

> A2: What is the maximum size of a file supported by your inode
> structure?  Show your work.

The inode structure contains 10 direct blocks, 1 indirect block, and 1 double indirect block. And each indirect block contains 128 entries. So the maximum size of a file supported by our inode structure is 10 + 128 + 128 * 128 = 16522 blocks, which is 16522 * 512 = 8454144 bytes.

#### SYNCHRONIZATION 

> A3: Explain how your code avoids a race if two processes attempt to
> extend a file at the same time.

We use a lock to protect the inode. When a process wants to extend a file, it should first acquire the lock of the inode. Then it can safely extend the file.

> A4: Suppose processes A and B both have file F open, both
> positioned at end-of-file.  If A reads and B writes F at the same
> time, A may read all, part, or none of what B writes.  However, A
> may not read data other than what B writes, e.g. if B writes
> nonzero data, A is not allowed to see all zeros.  Explain how your
> code avoids this race.

When A tries to read beyond the end of the file, it will first acquire the lock of the inode. Since B is writing the file, it will also acquire the lock of the inode. So A will wait until B finishes writing the file. Then A can safely read the file.

> A5: Explain how your synchronization design provides "fairness".
> File access is "fair" if readers cannot indefinitely block writers
> or vice versa.  That is, many processes reading from a file cannot
> prevent forever another process from writing the file, and many
> processes writing to a file cannot prevent another process forever
> from reading the file.

A lock is used to protect the inode. However, the lock is not held when reading or writing the file, unless the file is being extended. Once the expansion is finished, the lock is released. So many processes reading from a file cannot prevent forever another process from writing the file, and many processes writing to a file cannot prevent another process forever from reading the file.

#### RATIONALE 

> A6: Is your inode structure a multilevel index?  If so, why did you
> choose this particular combination of direct, indirect, and doubly
> indirect blocks?  If not, why did you choose an alternative inode
> structure, and what advantages and disadvantages does your
> structure have, compared to a multilevel index?

Yes, our inode structure is a multilevel index. We choose this particular combination of direct, indirect, and doubly indirect blocks because it is recommended in the Pintos Guide. The advantage of this structure is that it is easy to implement. The disadvantage is that it is not space efficient.

                SUBDIRECTORIES
                ==============

#### DATA STRUCTURES 

> B1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

```c
// src/filesys/inode.h

...

enum inode_type
  {
    INODE_FREEMAP,
    INODE_FILE,
    INODE_DIR
  };

struct inode_disk
  {
    ...
    enum inode_type type;               /* File or directory. */
    ...
  };

...

// src/userprog/process.h

...

struct file_elem
  {
    ...
    enum inode_type type;
    void* file;
    ...
  }; 

...

// src/thread/thread.h

...

struct thread
  {
    ...
    struct dir* current_dir;            /* Current directory. */
    ...
  };

...

```

- `enum inode_type`: The type of inode.
    - `INODE_FREEMAP`: The inode is used to store the free map.
    - `INODE_FILE`: The inode is used to store a file.
    - `INODE_DIR`: The inode is used to store a directory.

- In `struct inode_disk`:
    - `type`: The type of the inode.

- In `struct file_elem`:
    - `type`: The type of the file.
    - `file`: The file.

- In `struct thread`:
    - `current_dir`: The current directory of the thread.

#### ALGORITHMS 

> B2: Describe your code for traversing a user-specified path.  How
> do traversals of absolute and relative paths differ?

The user-specified path is first parsed into a list of names. Then the list is traversed from a given directory. If the path is absolute, the given directory is the root directory. If the path is relative, the given directory is the current directory of the current thread. If any name in the path is invalid, the traversal fails.

#### SYNCHRONIZATION 

> B4: How do you prevent races on directory entries?  For example,
> only one of two simultaneous attempts to remove a single file
> should succeed, as should only one of two simultaneous attempts to
> create a file with the same name, and so on.

A lock is used to protect the directory entries. When a process wants to remove a file, it should first acquire the lock of the directory. Then it can safely remove the file. When a process wants to create a file, it should first acquire the lock of the directory. Then it can safely create the file.

> B5: Does your implementation allow a directory to be removed if it
> is open by a process or if it is in use as a process's current
> working directory?  If so, what happens to that process's future
> file system operations?  If not, how do you prevent it?

Yes, our implementation allows a directory to be removed if it is open by a process or if it is in use as a process's current working directory. If a process's current working directory is removed, its inode will be marked as removed. So its future file system operations will fail. The directory will be freed only when all the processes close it.

#### RATIONALE 

> B6: Explain why you chose to represent the current directory of a
> process the way you did.

We choose to represent the current directory of a process as a pointer to a directory. And when it is `NULL`, it falls back to the root directory. This is because it is easy to implement. And specifying `NULL` to the root directory is because the initial thread is created before the file system is initialized and cannot access the root directory at that time.

                 BUFFER CACHE
                 ============

#### DATA STRUCTURES 

> C1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

```c
// src/filesys/cache.h

...

struct cache_entry 
  {
    block_sector_t sector;
    bool dirty;
    bool valid;
    bool accessed;
    uint8_t data[BLOCK_SECTOR_SIZE];
    struct lock lock;
  };

...

// src/filesys/cache.c

...

static struct cache_entry cache[CACHE_SIZE];
static struct lock cache_lock;

static struct list read_ahead_list;
static struct lock read_ahead_lock;
static struct condition read_ahead_cond;

static struct list write_behind_list;
static struct lock write_behind_lock;

...

```

- `struct cache_entry`: The cache entry.
    - `sector`: The sector number of the cache entry.
    - `dirty`: Whether the cache entry is dirty.
    - `valid`: Whether the cache entry is valid.
    - `accessed`: Whether the cache entry is accessed.
    - `data`: The data of the cache entry.
    - `lock`: The lock of the cache entry.

- `cache`: The cache.
- `cache_lock`: The lock of the cache.
- `read_ahead_list`: The list of read-ahead.
- `read_ahead_lock`: The lock of the list of read-ahead.
- `read_ahead_cond`: The condition variable of the list of read-ahead.
- `write_behind_list`: The list of write-behind.
- `write_behind_lock`: The lock of the list of write-behind.

#### ALGORITHMS 

> C2: Describe how your cache replacement algorithm chooses a cache
> block to evict.

We use the clock algorithm to choose a cache block to evict. We first check the accessed bit of each cache block. If it is set, we clear it and move to the next cache block. If it is not set, we evict the cache block. If all the accessed bits are set, we evict the first cache block.

> C3: Describe your implementation of write-behind.

A daemon thread is created to write-behind. It will write the dirty cache blocks to disk every `CACHE_FLUSH_INTERVAL` ticks. The daemon thread will
exit when the file system is shut down.

> C4: Describe your implementation of read-ahead.

A daemon thread is created to read-ahead. A monitor is implemented to synchronize the daemon thread and the other threads. The read-ahead requests are sent to the daemon thread through the monitor. The daemon thread will exit when the file system is shut down.

#### SYNCHRONIZATION 

> C5: When one process is actively reading or writing data in a
> buffer cache block, how are other processes prevented from evicting
> that block?

A lock is used to protect the cache entry. When a process is actively reading or writing data in a buffer cache block, it should first acquire the lock of the cache entry. Then it can safely read or write the data.

> C6: During the eviction of a block from the cache, how are other
> processes prevented from attempting to access the block?

A lock is used to protect the cache entry. When a process wants to access a block, it should first acquire the lock of the cache entry. Then it can safely access the block.

#### RATIONALE 

> C7: Describe a file workload likely to benefit from buffer caching,
> and workloads likely to benefit from read-ahead and write-behind.

A file workload likely to benefit from buffer caching is a workload that reads and writes the same data repeatedly. Workloads likely to benefit from read-ahead and write-behind are workloads that read and write data sequentially.

               SURVEY QUESTIONS
               ================

Answering these questions is optional, but it will help us improve the
course in future quarters.  Feel free to tell us anything you
want--these questions are just to spur your thoughts.  You may also
choose to respond anonymously in the course evaluations at the end of
the quarter.

> In your opinion, was this assignment, or any one of the three problems
> in it, too easy or too hard?  Did it take too long or too little time?

> Did you find that working on a particular part of the assignment gave
> you greater insight into some aspect of OS design?

> Is there some particular fact or hint we should give students in
> future quarters to help them solve the problems?  Conversely, did you
> find any of our guidance to be misleading?

> Do you have any suggestions for the TAs to more effectively assist
> students in future quarters?

> Any other comments?
