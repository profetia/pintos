#include "filesys/inode.h"
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

#ifdef FS
#include "filesys/cache.h"
#endif

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#ifdef FS
struct indirect_block
  {
    block_sector_t blocks[BLOCK_SECTOR_SIZE / sizeof (block_sector_t)];
  };
#endif

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

#ifdef FS
  if (pos < inode->data.length)
    {
      block_sector_t block = pos / BLOCK_SECTOR_SIZE;
      if (block < NUM_DIRECT_SECTORS)
        return inode->data.blocks[block];
      
      if (block < NUM_DIRECT_SECTORS + NUM_INDIRECT_SECTORS)
        {
          struct indirect_block indirect_block;
          cache_read (inode->data.blocks[NUM_DIRECT_BLOCKS], &indirect_block);
          return indirect_block.blocks[block - NUM_DIRECT_SECTORS];
        }
      
      if (block < NUM_DIRECT_SECTORS + NUM_INDIRECT_SECTORS + 
          NUM_DOUBLE_INDIRECT_SECTORS)
        {
          struct indirect_block indirect_block;
          cache_read (inode->data.blocks[NUM_DIRECT_BLOCKS + 
              NUM_INDIRECT_BLOCKS], &indirect_block);
          block_sector_t indirect_block_idx = 
              (block - NUM_DIRECT_SECTORS - NUM_INDIRECT_SECTORS) / 
              (BLOCK_SECTOR_SIZE / sizeof (block_sector_t));
          cache_read (indirect_block.blocks[indirect_block_idx], 
              &indirect_block);
          return indirect_block.blocks[(block - NUM_DIRECT_SECTORS - 
              NUM_INDIRECT_SECTORS) % (BLOCK_SECTOR_SIZE / 
              sizeof (block_sector_t))];
        }

      return -1;
    }

    return -1;
#else
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
#endif
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

#ifdef FS
static uint8_t zeros[BLOCK_SECTOR_SIZE];
static uint8_t errors[BLOCK_SECTOR_SIZE];
#endif

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
#ifdef FS
  memset (zeros, 0, BLOCK_SECTOR_SIZE);
  memset (errors, 0xff, BLOCK_SECTOR_SIZE);
#endif
}

#ifdef FS
/* Allocates a sector and writes the given data to it.
   Returns the sector number if successful, or BLOCK_SECTOR_ERROR
   if no sectors are available. */
static block_sector_t
sector_alloc (bool errored)
{
  block_sector_t sector = BLOCK_SECTOR_ERROR;

  if (!free_map_allocate (1, &sector))
    return BLOCK_SECTOR_ERROR;

  if (errored)
    cache_write (sector, errors);
  else
    cache_write (sector, zeros);

  return sector;
}

/* Expands the given block to the given number of sectors.
   Returns the number of sectors left to expand, or
   BLOCK_SECTOR_ERROR if expansion fails. */
static block_sector_t
block_expand (block_sector_t* block, block_sector_t sectors)
{
  if (*block == BLOCK_SECTOR_ERROR)
    {
      *block = sector_alloc (false);
      if (*block == BLOCK_SECTOR_ERROR)
        return BLOCK_SECTOR_ERROR;
    }

  return sectors - 1;
}

/* Expands the given indirect block to the given number of sectors.
   Returns the number of sectors left to expand, or
   BLOCK_SECTOR_ERROR if expansion fails. */
static block_sector_t
indirect_block_expand (block_sector_t* block, block_sector_t sectors)
{
  if (*block == BLOCK_SECTOR_ERROR)
    {
      *block = sector_alloc (true);
      if (*block == BLOCK_SECTOR_ERROR)
        return BLOCK_SECTOR_ERROR;
    }

  struct indirect_block indirect_block;
  cache_read (*block, &indirect_block);

  for (size_t i = 0; i < BLOCK_SECTOR_SIZE / sizeof (block_sector_t); ++i)
    {
      sectors = block_expand (&indirect_block.blocks[i], 
          sectors);
      if (sectors == BLOCK_SECTOR_ERROR)
        return BLOCK_SECTOR_ERROR;
      if (sectors == 0)
        {
          cache_write (*block, &indirect_block);
          return 0;
        }
    }

  cache_write (*block, &indirect_block);
  return sectors;
}

/* Expands the given double indirect block to the given number of sectors.
   Returns the number of sectors left to expand, or
   BLOCK_SECTOR_ERROR if expansion fails. */
static block_sector_t
double_indirect_block_expand (block_sector_t* block, block_sector_t sectors)
{
  if (*block == BLOCK_SECTOR_ERROR)
    {
      *block = sector_alloc (true);
      if (*block == BLOCK_SECTOR_ERROR)
        return BLOCK_SECTOR_ERROR;
    }

  struct indirect_block indirect_block;
  cache_read (*block, &indirect_block);

  for (size_t i = 0; i < BLOCK_SECTOR_SIZE / sizeof (block_sector_t); ++i)
    {
      sectors = indirect_block_expand (
          &indirect_block.blocks[i], sectors);
      if (sectors == BLOCK_SECTOR_ERROR)
        return BLOCK_SECTOR_ERROR;
      if (sectors == 0)
        {
          cache_write (*block, &indirect_block);
          return 0;
        }
    }

  cache_write (*block, &indirect_block);
  return sectors;
}

/* Expands the given inode to the given number of sectors.
   Returns true if successful, or false if expansion fails. */
static bool
inode_expand (struct inode_disk* inode_disk, block_sector_t sectors)
{
  ASSERT (inode_disk != NULL);
  ASSERT (sectors <= NUM_DIRECT_SECTORS + NUM_INDIRECT_SECTORS + 
      NUM_DOUBLE_INDIRECT_SECTORS);

  if (sectors == 0)
   return true;

  for (size_t i = 0; i < NUM_DIRECT_BLOCKS; ++i)
    {
      sectors = block_expand (&inode_disk->blocks[i], sectors);
      if (sectors == BLOCK_SECTOR_ERROR)
        return false;
      if (sectors == 0)
        return true;
    }

  for (size_t i = NUM_DIRECT_BLOCKS; 
          i < NUM_DIRECT_BLOCKS + NUM_INDIRECT_BLOCKS; ++i)
    {
      sectors = indirect_block_expand (
          &inode_disk->blocks[i], sectors);
      if (sectors == BLOCK_SECTOR_ERROR)
        return false;
      if (sectors == 0)
        return true;
    }

  for (size_t i = NUM_DIRECT_BLOCKS + NUM_INDIRECT_BLOCKS; 
          i < NUM_DIRECT_BLOCKS + NUM_INDIRECT_BLOCKS + 
          NUM_DOUBLE_INDIRECT_BLOCKS; ++i)
    {
      sectors = double_indirect_block_expand (
          &inode_disk->blocks[i], sectors);
      if (sectors == BLOCK_SECTOR_ERROR)
        return false;
      if (sectors == 0)
        return true;
    }

  return false;
}
#endif

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (
#ifdef FS
  enum inode_type type,
#endif
  block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

#ifdef FS
  ASSERT ((NUM_DIRECT_SECTORS + NUM_INDIRECT_SECTORS + 
      NUM_DOUBLE_INDIRECT_SECTORS) * BLOCK_SECTOR_SIZE >= (block_sector_t)8388608);
  // The maximum file size is 8 MB.
#endif

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
#ifdef FS
      disk_inode->type = type;
      memset (disk_inode->blocks, 0xff, sizeof (disk_inode->blocks));
      if (inode_expand (disk_inode, sectors))
#else
      if (free_map_allocate (sectors, &disk_inode->start)) 
#endif      
        {
#ifdef FS          
          cache_write (sector, disk_inode);
#else
          block_write (fs_device, sector, disk_inode);  
#endif  

#ifndef FS        
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                block_write (fs_device, disk_inode->start + i, zeros);               
            }
#endif          
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
#ifdef FS  
  cache_read (inode->sector, &inode->data);
#else
  block_read (fs_device, inode->sector, &inode->data);
#endif
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

#ifdef FS
static void
inode_delete (struct inode *inode);
#endif

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
#ifdef FS
          inode_delete (inode);
#else
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
#endif
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
#ifdef FS          
          cache_read (sector_idx, buffer + bytes_read);
#else
          block_read (fs_device, sector_idx, buffer + bytes_read);
#endif
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }          
#ifdef FS
          cache_read (sector_idx, bounce);
#else
          block_read (fs_device, sector_idx, bounce);
#endif
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

#ifdef FS
  if (inode->data.length < offset + size)
    {
      size_t sectors = bytes_to_sectors (offset + size);
      if (!inode_expand (&inode->data, sectors))
        return 0;
      inode->data.length = offset + size;
      cache_write (inode->sector, &inode->data);
    }
#endif

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
#ifdef FS
          cache_write (sector_idx, buffer + bytes_written);
#else
          block_write (fs_device, sector_idx, buffer + bytes_written);
#endif
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
#ifdef FS
            cache_read (sector_idx, bounce);
#else
            block_read (fs_device, sector_idx, bounce);
#endif
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
#ifdef FS        
          cache_write (sector_idx, bounce);
#else
          block_write (fs_device, sector_idx, bounce);
#endif
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

#ifdef FS
bool
inode_is_dir (const struct inode *inode)
{
  return inode->data.type == INODE_DIR;
}

bool
inode_is_removed (const struct inode *inode)
{
  return inode->removed;
}

static void
block_delete (block_sector_t block)
{
  ASSERT (block != BLOCK_SECTOR_ERROR);
  free_map_release (block, 1);
}

static void
indirect_block_delete (block_sector_t block)
{
  ASSERT (block != BLOCK_SECTOR_ERROR);
  struct indirect_block indirect_block;
  cache_read (block, &indirect_block);
  for (size_t i = 0; i < BLOCK_SECTOR_SIZE / sizeof (block_sector_t); ++i)
    {
      if (indirect_block.blocks[i] != BLOCK_SECTOR_ERROR)
        {
          block_delete (indirect_block.blocks[i]);
          indirect_block.blocks[i] = BLOCK_SECTOR_ERROR;
        }
    }
  block_delete (block);
}

static void
double_indirect_block_delete (block_sector_t block)
{
  ASSERT (block != BLOCK_SECTOR_ERROR);
  struct indirect_block indirect_block;
  cache_read (block, &indirect_block);
  for (size_t i = 0; i < BLOCK_SECTOR_SIZE / sizeof (block_sector_t); ++i)
    {
      if (indirect_block.blocks[i] != BLOCK_SECTOR_ERROR)
        {
          indirect_block_delete (indirect_block.blocks[i]);
          indirect_block.blocks[i] = BLOCK_SECTOR_ERROR;
        }
    }
  block_delete (block);
}

static void 
inode_delete (struct inode *inode)
{
  ASSERT (inode != NULL);
  ASSERT (inode->open_cnt == 0);
  ASSERT (inode->removed);

  for (size_t i = 0; i < NUM_DIRECT_BLOCKS; ++i)
    {
      if (inode->data.blocks[i] != BLOCK_SECTOR_ERROR)
        {
          block_delete (inode->data.blocks[i]);
          inode->data.blocks[i] = BLOCK_SECTOR_ERROR;
        }
    }

  for (size_t i = NUM_DIRECT_BLOCKS; 
          i < NUM_DIRECT_BLOCKS + NUM_INDIRECT_BLOCKS; ++i)
    {
      if (inode->data.blocks[i] != BLOCK_SECTOR_ERROR)
        {
          indirect_block_delete (inode->data.blocks[i]);
          inode->data.blocks[i] = BLOCK_SECTOR_ERROR;
        }
    }

  for (size_t i = NUM_DIRECT_BLOCKS + NUM_INDIRECT_BLOCKS; 
          i < NUM_DIRECT_BLOCKS + NUM_INDIRECT_BLOCKS + 
          NUM_DOUBLE_INDIRECT_BLOCKS; ++i)
    {
      if (inode->data.blocks[i] != BLOCK_SECTOR_ERROR)
        {
          double_indirect_block_delete (inode->data.blocks[i]);
          inode->data.blocks[i] = BLOCK_SECTOR_ERROR;
        }
    }

  cache_write (inode->sector, &inode->data);
  free_map_release (inode->sector, 1);
}
#endif