/* buffer node in the buffer cache linked list
 *
 * flags	information about the node
 * dev		number related to the device
 * blockno	block number corresponding to the cache entry
 * lock		spinlock for synchronization
 * refcnt	number of references to this cache entry
 * prev, next	linked list pointers (dll)
 * qnext	something related to a disk queue (not so sure)
 * data		the actual data of the on-disk block
 */
struct buf {
  int flags;
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  struct buf *qnext; // disk queue
  uchar data[BSIZE];
};
#define B_VALID 0x2  // buffer has been read from disk
#define B_DIRTY 0x4  // buffer needs to be written to disk

