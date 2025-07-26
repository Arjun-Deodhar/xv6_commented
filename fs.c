// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
static void itrunc(struct inode*);
// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb; 

// Read the super block.
/* reads the superblock (block number 1) from device number dev
 *
 * bread() reads the block number 1 and returns a pointer to struct buf
 * memmove() moves the data (an array of BSIZE bytes) into sb
 * brelse() releases the struct buf
 */
void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in cache: an entry in the inode cache
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a cache entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   cache entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays cached and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The icache.lock spin-lock protects the allocation of icache
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold icache.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

/* iinit() initialises the inode cache icache
 *
 * for each inode in icache (maximum of NINODE inodes can be stored in cache)
 * 	initsleeplock() is called, to lock the inodes
 *
 * then, te superblock is read from the disk into global variable sb
 */
void
iinit(int dev)
{
  int i = 0;
  
  initlock(&icache.lock, "icache");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&icache.inode[i].lock, "inode");
  }

  readsb(dev, &sb);
  cprintf("sb: size %d nblocks %d ninodes %d nlog %d logstart %d\
 inodestart %d bmap start %d\n", sb.size, sb.nblocks,
          sb.ninodes, sb.nlog, sb.logstart, sb.inodestart,
          sb.bmapstart);
}

static struct inode* iget(uint dev, uint inum);

//PAGEBREAK!
// Allocate an inode on device dev.
// Mark it as allocated by giving it type type.
// Returns an unlocked but allocated and referenced inode.

/* ialloc() allocates a node on device dev, of type type
 * the function returns an inode pointer
 *
 * iget() is called to get an in-memory entry for the inode in 
 * the icache
 */
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  /* for each inode number inum (1 <= inum < sb.ninodes (typically 200))
   * 	read block from device dev, with block number IBLOCK(inum, sb)
   * 	IBLOCK returns the block for inode number inum, using superblock sb
   *
   * 	then, a struct dinode *dip points to the proper offset into bp->data,
   * 	since one block contains BSIZE / sizeof(struct dinode) entries
   *
   *
   * 	if dip->type == 0
   * 		free inode obtained
   * 		the inode obtained is zeroed
   *		type is set to type
   *		block is written onto disk by log_write()
   *		lock on the block obtained in bread() is released
   *		iget() is called to allocate an in-memory entry in 
   *		the inode cache
   *
   *	else
   *		dip->type is non-zero, indicating that the inode is in use
   *		hence we release the lock on the block
   *
   * Q: should't there be an inner loop for checking for each of the
   * 8 inodes in a block? Won't this result in 7 less reads for every 8 inodes?
   */
  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk, since i-node cache is write-through.
// Caller must hold ip->lock.
/* iupdate() copies a modified inode entry in the icache onto the disk
 * ip should point to the entry in the cache
 *
 * first, the block containing the inode is read by bread()
 * dip points to the proper inode entry in the block bp->data
 * 
 * values inside ip are copied into dip
 * memmove() is called so that ip->addrs array is moved into dip->addrs
 * since this is an array, we cannot simply copy them
 *
 * log_write() writes the block back onto the disk
 *
 * lock on bp is released by brelse()
 */
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.

/* just returns a pointer to an entry in the inode cache which 
 * which matches device number and inode number dev and inum respectively
 *
 * iget() doesn't lock() the inode, nor does it read the inode from disk
 */
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  /* acquire() a lock on the icache
   */
  acquire(&icache.lock);

  // Is the inode already cached?
  empty = 0;
  /* for each inode pointed by ip in icache
   * 	if references > 0 and the device and inode numbers match
   * 		inode already exists in cache
   * 		increase the reference count by one
   * 		release the lock on icache, return ip
   *
   *	if the empty pointer is not set to some inode entry, and a 
   *	free entry is obtained
   *		make empty point to that empty entry (this will be used
   *		if the inode is not found in the cache)
   */
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  /* if the loop exists and empty is still NULL (0), this means that
   * there are no free inodes in the icache, and our inode was not found
   *
   * else, we set the ip to empty, and update
   * 	ip->dev	to 		dev
   * 	ip->inum to 	inum
   * 	ip->ref to 		1
   * 	ip->valid		to 0
   *
   * release() the lock obtained on icache
   *
   * Q: Why panic() if no inodes entries are empty in the cache?
   * A: (src: Maurice Bach book)
   * 	It is better to panic() here since inode allocation is controlled
   * 	indirectly by the user programs, based on the nature of syscalls
   * 	made by them. 
   *
   * 	As opposed to the buffer cache, whose nodes are allocated and de
   * 	allocated depending on the kernel, the buffer cache node allocation 
   * 	is not user controlled
   *
   * 	This is why we panic() here, and can sleep() while allocating nodes 
   * 	in the buffer cache
   *
   *	Ideally, the system call should have failed over here, but for simplicity
   *	reasons, xv6 has just called panic()
   */
  // Recycle an inode cache entry.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&icache.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.

/* idup() returns inode pointer ip, after incrementing its reference count
 * acquire() and release() are done for synchronization on the icache
 */
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.

/* ilock() locks the inode pointed to by ip
 * reads the inode from disk if the valid bit is 0
 */
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  /* if pointer is NULL or the reference count is less
   * than 1
   */
  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  /* acquiresleep() the lock first
   *
   * Q: difference between acquire() and acquiresleep()?
   * A: acquiresleep() is for a sleeplock, hence the process calling
   * it will sleep() if the resource is locked
   *
   * acquire(), on the other hand is a busy-wait lock, which will
   * make the process enter an infinite loop and wait for the lock
   * on the resource to be released
   */
  acquiresleep(&ip->lock);

  /* valid bit is 0 means that the inode has not been read from
   * the disk yet
   *
   * hence, bread() the block into memory, and make dip point to the correct
   * inode entry by arithmetic
   *
   * copy the type, major, minor, nlink, size entries from dip into ip
   * memmove() to copy the addrs array from dip into ip
   *
   * release the lock on bp, and set the valid bit of ip to 1
   * ensure that the type is not 0
   */
  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
/* iunlock() unlocks the inode pointed to by ip
 *
 * if the pointer is NULL, or the lock isnt held, or ref < 1
 * 	error
 * else
 * 	releasesleep() the lock
 */
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.

/* iput() drops a reference to an in-memory inode (typically in icache)
 * if the link count drops to zero, we can empty this entry
 * 
 * Q: Difference between nlink and ref for in-memory inodes?
 */
void
iput(struct inode *ip)
{
  acquiresleep(&ip->lock);
  /* if the cache entry is valid and the nlink is 0
   * 	acquire() lock on the icache
   *	store the ip->ref field in r
   *	release() the lock on icache
   *  
   *  	if r is 1 (i.e. last reference)
   *  		itrunc() the ip to discard its contents
   *  		set type to 0
   *  		iupdate() to write the inode back to disk
   *  		set valid bit of icache entry to 0
   * releasesleep() the lock on ip
   *
   * acquire() the lock on icache
   * decrement ref count
   * release() the lock on icache
   */
  if(ip->valid && ip->nlink == 0){
    acquire(&icache.lock);
    int r = ip->ref;
    release(&icache.lock);
    if(r == 1){
      // inode has no links and no other references: truncate and free.
      itrunc(ip);
      ip->type = 0;
      iupdate(ip);
      ip->valid = 0;
    }
  }
  releasesleep(&ip->lock);

  acquire(&icache.lock);
  ip->ref--;
  release(&icache.lock);
}

// Common idiom: unlock, then put.
/* ensures that the inode is unlocked and then put
 */
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

//PAGEBREAK!
// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.

/* bmap() returns the disk block number of block number bn in inode ip
 *
 * if there is no such block, then then bmap allocates one
 */
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  /* if the block number is one of the direct blocks
   * 	set addr to the bnth entry of ip->addrs
   * 	if the block address addr is free (addr == 0)
   * 		balloc() a block, store address in addr
   * 	return addr
   */
  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  /* since the execution came here, bn >= NDIRECT
   *
   * in this case, we are looking for the (bn - NDIRECT)th block of
   * the file, which will be the (bn - NDIRECT)th entry in the block
   * ip->addrs[NDIRECT]
   */
  bn -= NDIRECT;

  /* ensure that bn is less than the allowed number of NINDIRECT
   * blocks
   */
  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    /* set addr to ip->addrs[NDIRECT], i.e. the indirect block
     *
     * if indirect block is yet to be allocated
     * 		balloc() it, store its address in addr
     *
     * bread() block addr from device ip->dev, into struct buf pointed to by bp
     * 
     * a points to the data of the block read
     *
     * if a[bn] is free (0)
     * 		balloc() a block and store its address in a[bn]
     * 		log_write() bp back to memory
     *
     * release the lock on buffer pointed to by bp
     * return addr
     */
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).

/* itrunc() "frees" the data blocks "pointed to" by an inode struture, 
 * when no directory entry refers to it
 * 
 * all the blocks that were balloc()ed for the file need to be bfree()d
 * for this, we need to travserse the direct as well as indirect blocks
 * and call bfree() for each
 */
static void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  /* free each of the allocated direct block
   *
   * set ip->addrs[i] to zero after freeing
   * Q: Why is it set to zero?
   */
  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  /* now, read the indirect block into memory, if it exists
   *
   * a points to the data of the block
   *
   * for each block address entry in a
   * 	if the block is balloc()ed
   * 		bfree() it
   * release the lock on buffer pointed to by bp
   * bfree() the indirect block read into memory
   *
   * set size of file to 0
   * write inode back to disk by iupdate()
   *
   * Q: Why is type not set to zero here?
   * A: maybe because this function is just meant to bfree() the data blocks?
   */
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

//PAGEBREAK!
// Read data from inode.
// Caller must hold ip->lock.
/* readi() reads n bytes from file with inode pointed to by ip, to destination dst
 * the reading will start at the byte offset off
 * returns the total number of bytes read
 */
int
readi(struct inode *ip, char *dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  /* if the type of the inode is a device file
   * 	if ip->major less than 0, or greater than the number of devices
   * 	of the function pointer is NULL
   * 		return -1
   *
   * 	else
   * 		return the value returned by read(ip, dst, n) where
   * 		read() is a function pointer to the read function of 
   * 		that device, from the devsw[] array, indexed
   * 		by ip->major (device number)
   *
   * (not so sure about major and minor device numbers as of now)
   */
  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);
  }

  /* if the offset is greater than file size, or the number of
   * bytes to be read, once added to off overflows
   * 	return -1
   */
  if(off > ip->size || off + n < off)
    return -1;
  
  /* if the number of bytes to be read at offset off exceeds the
   * size of the file, then set n to ip->size - off (this is when read()
   * is called for n number of bytes, but there are less than n bytes in the
   * file at byte offset off
   */
  if(off + n > ip->size)
    n = ip->size - off;

  /* for n bytes to be read
   *
   * 	call bmap() to get the block address of block number off / BSIZE
   * 	from inode pointed to by ip
   *
   * 	bread() the block address from device dev, and block number
   * 	returned by bmap()
   *
   * 	set m to the minimum of (n - tot) and (BSIZE - off % BSIZE)
   * 	memmove() to copy m bytes from bp->data, at offset off % BSIZE
   * 	into address dst
   *
   *	increment tot, odd and dst by m 
   *
   * 	release the lock obtained on the buffer pointed to by bp
   *
   * return the number of bytes that were to be read
   */
  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(dst, bp->data + off%BSIZE, m);
    brelse(bp);
  }
  return n;
}

// PAGEBREAK!
// Write data to inode.
// Caller must hold ip->lock.

/* similar to the readi() function, but performs a write
 *
 * some error handling is different
 */
int
writei(struct inode *ip, char *src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
      return -1;
    return devsw[ip->major].write(ip, src, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(bp->data + off%BSIZE, src, m);
    log_write(bp);
    brelse(bp);
  }

  if(n > 0 && off > ip->size){
    ip->size = off;
    iupdate(ip);
  }
  return n;
}

//PAGEBREAK!
// Directories

/* wrapper for strncmp() fuction, which is called as
 * strncmp(s, t, DIRSIZ)
 *
 * return value of namecmp() is same as that of strncmp()
 */
int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
/* dirlookup() looks for a directory entry with the name
 * "name" in directory file's inode to which dp points
 *
 * poff will be set to the offset of that entry in the
 * directory file
 *
 * returns an inode pointer to the inode of the file whose name
 * is same as "name" (if the file is found inside the directory)
 */
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  /* ensure that the dp is of type directory
   *
   * if it is not a directory, then panic()
   */
  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  /* linear search for all the directory entries in the dir
   *
   * readi() will read sizeof(de) bytes at file offset off
   * from the file poitned to by dp (which is the directory file 
   * itself
   *
   * the bytes read will be stored into variable de
   *
   * if the inode number in that de is zero, this means that the
   * entry has not been allocated yet
   *
   * Q: How will this happen? does this mean that entries are not
   * set sequentially? Or will this happen if a file in the directory
   * is removed and a "hole" is created?
   *
   * if the name of the file in the entry matches "name"
   * 	set *poff (if poff is not NULL) to the
   * 	current value of off
   *
   * 	set inum to the inum in the directory entry, so that the 
   * 	inode pointer can be obtained by iget()
   *
   * 	call iget() to get the inode pointer to inode with number inum
   * 	on device dp->dev
   *
   * 	return this inode number
   *
   * if the file is not found
   * 	return NULL
   */
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.

/* dirlink() inserts a directory entry into the directory file whose
 * inode is pointed to by dp
 *
 * the name of the file and its inode number are passed as parameters
 *
 * returns 0 to indicte success
 * else returns -1
 */
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  /* if the file (or dir :) ) with this name already exists
   * 	return -1 to indicate error
   *
   * this is because there cannot be two files, directories
   * or a file and a directory with the same name inside
   * one directory
   */
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  /* for all the directory entries in the file
   *
   * 	readi() to read sizeof(de) bytes into variable de,
   * 	at offset off into the file whose inode is pointed to by dp
   *
   * 	if inum is zero in the de
   * 		this means that the entry is free
   * 		break from the loop, since we found a free entry
   */
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  /* strncpy() the name passed as a parameter into the directory
   * entry obtained from the for loop above
   *
   * set the inum of that directory entry to inum
   *
   * write that directory entry back, using writei()
   * at offset off into the file of the directory
   * 
   * Q: It is possible that all the available directory entries
   * have been allocate. How is this handled, since anyways we are
   * copying the name and inum passed by the called into de without 
   * checking if it is actually free
   */
  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

//PAGEBREAK!
// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//

/* self explanatory, all this does is that it gets the next element
 * from the pathname "path", skipping all the slashes in between
 *
 * name is set with the next path element in the path
 * it points to the path element
 *
 * if there is no next element, then return value will be NULL
 * indicating that the number of path elements are over
 *
 * if path[0] is '\0', then this means that the element returned 
 * was the last one
 */
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  /* skip all '/'
   *
   * if the path is over, this means that there is no
   * next element remaining, hence return NULL
   *
   * set s to path, the first non-slash non-nul byte in path
   *
   * while *path is not a slash and not nul, increment path
   *
   * store the length of the string in len, by path - s
   * if the len is >= DIRSIZ 
   * 	memmove() DIRSIZ bytes from s into name
   * else
   * 	memmove() len nbytes from s into name
   * 	"stringify" name, since the nul byte was nont included
   *
   * Q: When will len be >= DIRSIZ
   * A: Never, maybe this is just to make the skipelem() more generic
   *
   * Now, skip all the further '/' and return path
   * if *path is '\0' and a non NULL value is returned, this
   * indicates that the element returned was the last in the path
   */
  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
/* 
 * return inode pointer to inode of the file or directory with name 
 * "name" in the path "path"
 *
 * nameiparent is a flag that returns either the parent's inode of 
 * name, or the actual inode pointer, depending on the value passed
 *
 */
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  /* if the path begins with '/'
   * 	this means that we need to search from the root directory
   * 	set ip to point to the inode of the root directory by iget()
   *
   * else
   * 	this means that we need to search from the current working
   * 	directory of the process
   * 	set ip to point to the inode of the current working directory
   * 	of the current process running (myproc() returns a pointer to
   * 	this process)
   *
   * 	idup() will be called to increment the reference to that inode by
   * 	1, (maybe: so that it will not be recycled by some other process)
   */
  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);

  /* for each path element in path
   * 	lock the inode pointed to by ip
   *
   * 	if the type of the inode is not a directory
   * 		unlock() ip
   * 		return 0, since it should be a directory
   */
  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
