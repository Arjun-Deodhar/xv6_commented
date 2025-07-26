// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
//
// The implementation uses two state flags internally:
// * B_VALID: the buffer data has been read from the disk.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"


/* This is a linked list of all the buffers in the buffer
 * cache on xv6
 *
 * buf[NBUF] is an array of (3 * MAXOPBLOCKS) struct bufs, which 
 * is 30
 *
 * the struct buf are thus stored in an array, but are connected
 * in a doubly circular linked list (dcll)
 */
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

/* initialise the buffer cache by setting the pointers
 * of each element in the array buf[] to their proper values, 
 * thus creating the dcll data structure, a doubly
 * circular linked list, to be more precise
 *
 * bcache.head is the head node of the linked list, which is
 * stored separate from the other nodes in the dcll
 *
 * the linked list is used as:
 * 		MRU from head
 * 		LRU from tail
 */
void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

//PAGEBREAK!
  // Create linked list of buffers
  /* initially set the prev and next pointers of the head
   * to point to the head of the list
   *
   * then for each node b in buf[], 4 pointers must change
   *
   *	make head.next point to b
   * 	make b.prev point to head
   * 	make b.next point to the previously inserted node
   */
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.

/* bget() scans the dcll of the buffer cache
 *
 * first it acquires a lock on the head of the bcache itself
 *
 * now, it iterates over all the struct buf in the dcll
 *
 * if device number dev and block number blockno match:
 * 		reference count of that struct buf is incremented, since some new
 * 		process is accessing that buffer
 *
 * 		it then releases the lock held on bcache, acquires the sleeplock on
 * 		struct buf, and returns a pointer to that buffer after the sleeplock
 * 		has been acquired
 *
 * 		hence, if another process is using that buffer, then sleeplock
 * 		ensures that the other process wakes the process calling bget()
 *
 * if the buffer is not found in the bcache, bget() must allocate a new one
 * bget() WILL NOT read the buffer from the disk. It just allocates an entry 
 * in the buffer cache
 */
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached; recycle an unused buffer.
  // Even if refcnt==0, B_DIRTY indicates a buffer is in use
  // because log.c has modified it but not yet committed it.
  /* scan the list from prev, which is a dcll maintained by struct buf
   * this is to recycle the least recently used buffer
   *
   * if the reference count is zero, and B_DIRTY is cleared, then we
   * can use this buffer
   *
   * set 
   * b->dev to dev
   *
   * b->blockno to blockno
   *
   * b->flags to 0, to mark the struct buf as invalid, so that bread() will read
   * the buffer from the disk
   *
   * b->refcnt to 1
   *
   * release() the lock on bcache and acquire the sleeplock on struct buf
   * return a pointer to the buf obtained
   */
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->flags = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
/* calls bget() to obtain an entry in the buffer cache
 * bget() will return when either:
 * 		buffer is found in cache, with valid data
 * 		in this case, bread() simply returns the pointer to the buffer
 *
 * 		else, buffer is not found and hence new buffer is allocated
 * 		this means that the data needs to be read from the disk
 *
 * if the flags of *b have been set to 0, this means that the
 * buffer is invalid, and it needs to be read from the disk
 * hence, bread() calls iderw() on b
 *
 * b is then returned
 */
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if((b->flags & B_VALID) == 0) {
    iderw(b);
  }
  return b;
}

// Write b's contents to disk.  Must be locked.

/* sets b->flags to B_DIRTY, so that iderw() will write it to disk
 */
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  b->flags |= B_DIRTY;
  iderw(b);
}

// Release a locked buffer.
// Move to the head of the MRU list.

/* locked buffer is released
 *
 * releasesleep() is called to release the individual struct buf
 * this will awaken any processes that are waiting for the buffer
 *
 * b->refcnt is decremented
 *
 * lock acquired on bcache
 *
 * then, the bcache dcll must be changed, such that b is moved to
 * the head of the MRU list, if b->refcnt reaches 0
 *
 * for this, brelse() acquires a lock on bcache, and then does pointer
 * manipulations to move the buffer to the head
 *
 * lock on bcache is then released
 */
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}
//PAGEBREAK!
// Blank page.

