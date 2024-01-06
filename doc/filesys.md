             +-------------------------+
             |         CS 140          |
             | PROJECT 4: FILE SYSTEMS |
             |     DESIGN DOCUMENT     |
             +-------------------------+

#### GROUP 

>> Fill in the names and email addresses of your group members.

Linshu Yang <yanglsh@shanghaitech.edu.cn>
Lei Huang <huanglei@shanghaitech.edu.cn>

#### PRELIMINARIES 

>> If you have any preliminary comments on your submission, notes for the
>> TAs, or extra credit, please give them here.

>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.

             INDEXED AND EXTENSIBLE FILES
             ============================

#### DATA STRUCTURES 

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

```cpp
/* On-disk indirect block.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct indirect_block_disk_t
{
  block_sector_t direct_blocks[128 - 1];
  unsigned magic;                     /* Magic number. */
};

/* On-disk double indirect block.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct double_indirect_block_disk_t
{
  block_sector_t indirect_block_disk[128 - 1];
  unsigned magic;                     /* Magic number. */
};
/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    block_sector_t direct_blocks[12];  /* Direct blocks. */
    block_sector_t indirect_block;      /* Indirect block. */
    block_sector_t double_indirect_block[2]; /* Double indirect block. */
    bool isdir;                         /* Is a directory or not. default: not. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[110];               /* Not used. */
  };

struct inode
    {
        ...
        struct lock lock;               /* Inode lock. */
        ...
    }

```

>> A2: What is the maximum size of a file supported by your inode
>> structure?  Show your work.

(12 + 127 + 2 * 127 * 127) * 512 = 15 MiB. This can be easily scaled up by changing 

```cpp
#define DOUBLE_INDIRECT_BLOCK_IN_INODE_SIZE 2
```

```cpp
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    block_sector_t direct_blocks[12];  /* Direct blocks. */
    block_sector_t indirect_block;      /* Indirect block. */
    block_sector_t double_indirect_block[2]; /* Double indirect block. */
    bool isdir;                         /* Is a directory or not. default: not. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[110];               /* Not used. */
  };
```

My inode structure is a multilevel index. The maximum size of a file supported by my inode structure is (12 + 127 + 112 * 127 * 127) * 512 = 882.008 MiB.


#### SYNCHRONIZATION 

>> A3: Explain how your code avoids a race if two processes attempt to
>> extend a file at the same time.

We use a lock to protect the inode. When a process wants to extend a file, it should first acquire the lock. Then it can safely extend the file. After that, it should release the lock.


>> A4: Suppose processes A and B both have file F open, both
>> positioned at end-of-file.  If A reads and B writes F at the same
>> time, A may read all, part, or none of what B writes.  However, A
>> may not read data other than what B writes, e.g. if B writes
>> nonzero data, A is not allowed to see all zeros.  Explain how your
>> code avoids this race.

The lock is used to protect the inode. When a process wants to read or write a file, it should first acquire the lock. Then it can safely read or write the file. After that, it should release the lock.


>> A5: Explain how your synchronization design provides "fairness".
>> File access is "fair" if readers cannot indefinitely block writers
>> or vice versa.  That is, many processes reading from a file cannot
>> prevent forever another process from writing the file, and many
>> processes writing to a file cannot prevent another process forever
>> from reading the file.

Unless the file is being extended forever (which would exhaust the system after a while), the lock is only held for a short time after the reading or writing. So the fairness is guaranteed.

---- RATIONALE ----

>> A6: Is your inode structure a multilevel index?  If so, why did you
>> choose this particular combination of direct, indirect, and doubly
>> indirect blocks?  If not, why did you choose an alternative inode
>> structure, and what advantages and disadvantages does your
>> structure have, compared to a multilevel index?

Yes. I choose this particular combination of direct, indirect, and doubly indirect blocks because it is easy to implement and it can support large files.

                SUBDIRECTORIES
                ==============

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

```cpp
struct inode_disk
  {
        ...
        bool isdir;                         /* Is a directory or not. default: not. */
        ...
  };
struct thread
  {
        ...
        int cwd_fd;                          /* Current working directory. */
        ...
  };
```

---- ALGORITHMS ----

>> B2: Describe your code for traversing a user-specified path.  How
>> do traversals of absolute and relative paths differ?

First tokenize the path. Then traverse the path token by token. If the path is absolute, start from the root directory. If the path is relative, start from the current directory, which is stored in the thread struct. If the path is invalid, return NULL.

---- SYNCHRONIZATION ----

>> B4: How do you prevent races on directory entries?  For example,
>> only one of two simultaneous attempts to remove a single file
>> should succeed, as should only one of two simultaneous attempts to
>> create a file with the same name, and so on.

A lock is used to protect the directory entries. When a process wants to remove or create a file, it should first acquire the lock. Then it can safely remove or create the file. After that, it should release the lock.

>> B5: Does your implementation allow a directory to be removed if it
>> is open by a process or if it is in use as a process's current
>> working directory?  If so, what happens to that process's future
>> file system operations?  If not, how do you prevent it?

Yes. But after the current working directory is removed, any further operations on the directory system will fail. I assume the current working directory is not special. So it can be removed. 

---- RATIONALE ----

>> B6: Explain why you chose to represent the current directory of a
>> process the way you did.

I choose to represent the current directory of a process as a file descriptor. Because it is similar to the current working directory in UNIX.


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

On the road ahead, facing winds and rains is normal.
Some enterprises face operational pressures,
some people encounter difficulties in employment and life,
and some places suffer from floods, typhoons,
earthquakes, and other natural disasters, all of which I deeply care about.
Everyone faces challenges bravely and helps each other,
facing challenges and overcoming difficulties, which moves me deeply.
Hardworking farmers, diligent workers, entrepreneurs who dare to explore and innovate,
soldiers who defend the country,
and people from all walks of life are all sweating it out,
and every ordinary person has made extraordinary contributions!
The people are always our greatest reliance in overcoming all difficulties and challenges.

> Did you find that working on a particular part of the assignment gave
> you greater insight into some aspect of OS design?

> Is there some particular fact or hint we should give students in
> future quarters to help them solve the problems?  Conversely, did you
> find any of our guidance to be misleading?

This year, we have taken powerful steps.
Through persistent and sustained effort,
China's innovative drive and developmental vitality have surged forth.
The C919 large aircraft achieved commercial flight,
a domestically-made large cruise ship completed its trial voyage,
the Shenzhou family continued its space relay,
and the "Fendouzhe" reached extreme depths.
Popular domestic brands, high demand for new domestic smartphones,
and new energy vehicles, lithium batteries, and photovoltaic products
have added new highlights to Chinese manufacturing.
China is climbing with an unyielding spirit, creating wonders every day.

> Do you have any suggestions for the TAs to more effectively assist
> students in future quarters?

Currently, some parts of the world are still shrouded in the smoke of war.
The Chinese people deeply understand the preciousness of peace.
We are willing to work with the international community to focus on the future of humanity and the well-being of the people, to promote the building of a community with a shared future for mankind, and to create a better world.

> Any other comments?

At this moment, the night is colorful, and thousands of households are lit up. Let us together wish for the prosperity of our motherland, peace and tranquility in the world! I wish everyone warmth throughout the seasons and smooth and healthy lives!

Thank you all!