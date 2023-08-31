#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <list.h>
#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

#ifdef FS
enum inode_type
  {
    INODE_FREEMAP,
    INODE_FILE,
    INODE_DIR
  };
#endif

#ifdef FS
#define NUM_DIRECT_BLOCKS ((block_sector_t)10)
#define NUM_INDIRECT_BLOCKS ((block_sector_t)1)
#define NUM_DOUBLE_INDIRECT_BLOCKS ((block_sector_t)1)
#define NUM_BLOCKS ((block_sector_t)(NUM_DIRECT_BLOCKS + \
    NUM_INDIRECT_BLOCKS + NUM_DOUBLE_INDIRECT_BLOCKS))

#define NUM_DIRECT_SECTORS NUM_DIRECT_BLOCKS
#define NUM_INDIRECT_SECTORS NUM_INDIRECT_BLOCKS * \
    (BLOCK_SECTOR_SIZE / (block_sector_t)sizeof (block_sector_t))
#define NUM_DOUBLE_INDIRECT_SECTORS NUM_DOUBLE_INDIRECT_BLOCKS * \
    (BLOCK_SECTOR_SIZE / (block_sector_t)sizeof (block_sector_t)) * \
    (BLOCK_SECTOR_SIZE / (block_sector_t)sizeof (block_sector_t))

#endif

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
#ifdef FS
    block_sector_t blocks[NUM_BLOCKS];  /* Data blocks. */
    // The first NUM_DIRECT_POINTERS blocks are direct blocks.
    // The next NUM_INDIRECT_BLOCKS blocks are indirect blocks.
    // The last NUM_DOUBLE_INDIRECT_BLOCKS blocks are double indirect blocks.
    enum inode_type type;               /* File or directory. */
#else
    block_sector_t start;               /* First data sector. */
#endif
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
#ifdef FS
    uint8_t unused[504 - NUM_BLOCKS * sizeof(block_sector_t) - 
                    sizeof(enum inode_type)];
                                        /* Not used. */
#else
    uint32_t unused[125];               /* Not used. */
#endif
  };

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

void inode_init (void);
bool inode_create (
#ifdef FS
    enum inode_type type,
#endif    
    block_sector_t, off_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
