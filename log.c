#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
  int n;
  int block[LOGSIZE];
};

struct log {
  struct spinlock lock;
  int start;
  int size;
  int outstanding; // how many FS sys calls are executing.
  int committing;  // in commit(), please wait.
  int dev;         // on which deviec does this log exist
  struct logheader lh;
};
struct log log;

static void recover_from_log(void);
static void commit();

// initialises teh log data structure and recovers
// from the log, if any writes were pending
// during the crash
// this is called from forkret(), NOT in kernel's main()
// hence it will be basically called by init process!
void
initlog(int dev)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  struct superblock sb;
  initlock(&log.lock, "log");

  // readsb() is called in iinit() which is called in forkret() just
  // before the call to initlog(), thus the superblock must be in the 
  // buffer cache!
  // from there it is copied to a local struct superblock
  readsb(dev, &sb);
  // Q: lock not taken here since only one process is running?
  log.start = sb.logstart;
  log.size = sb.nlog;
  log.dev = dev;
  recover_from_log();
}

/* functions and what they do in short:
 *
 * install_trans()
 *  for all the non-commited blocks recorded in log.lh.block[] 
 *    bread the block from the log
 *    bread the block from its actual location on disk
 *    copy (in-memory) contents from log block to disk block
 *    bwrite the actual one back to disk
 *    brelse both blocks
 *
 * read_head()
 *  read log header from disk into the in-memory log header
 *  contents like lh.n and the lh.block[] array are updated
 *  in the in-memory log header from the one read from disk
 *
 * write_head()
 *  write the log header back to the disk
 *  this is the true part where the actual commit happens, and
 *  we can actually recover from a crash once this write 
 *  succeeds :)
 *
 *  this basically reads the log header block and then writes
 *  the lh.n and lh.block[] to the header on disk, which means that
 *  the block numbers that were to be written in a transaction are
 *  now on disk, in the log header
 * 
 * commit() performs the commit, see the comments in the code
 *
 * write_log()
 *  write the log blocks from cache to on-disk log
 *
 * log_write()
 *
 * recover_from_log() mimics the commit() function
 *  this basically calls the functions:
 *
 *  // read the log header
 *  read_head();
 *
 *  // transfer the contents of the blocks in the log to the
 *  // actual locations on disk
 *  install_trans();
 *
 *  // set lh.n to zero, so that write_head() writes
 *  // zero to the log header which as a result erases the log
 *  log.lh.n = 0;
 *   
 *  write_head(); // clear the log
 *
 */

// Copy committed blocks from log to their home location
static void
install_trans(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *lbuf = bread(log.dev, log.start+tail+1); // read log block
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst
    memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst
    bwrite(dbuf);  // write dst to disk
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
static void
read_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  log.lh.n = lh->n;
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i];
  }
  brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
static void
write_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
  }
  bwrite(buf);
  brelse(buf);
}

static void
recover_from_log(void)
{
  // read the log header
  read_head();

  // transfer the contents of the blocks in the log to the
  // actual locations on disk
  install_trans(); // if committed, copy from log to disk

  // set lh.n to zero, so that write_head() writes
  // zero to the log header which as a result erases the log
  log.lh.n = 0;

  write_head(); // clear the log
}

// called at the start of each FS system call.
/* this is used to record logs about the FS operations
 * not really sure what this does as of now, will see later
 */
void
begin_op(void)
{
  acquire(&log.lock);
  while(1){
    if(log.committing){
      sleep(&log, &log.lock);
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
      // this op might exhaust log space; wait for commit.
      sleep(&log, &log.lock);
    } else {
      log.outstanding += 1;
      release(&log.lock);
      break;
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){
    do_commit = 1;
    log.committing = 1;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
  }
  release(&log.lock);

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}

// Copy modified blocks from cache to log.
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1); // log block
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // write the log
    brelse(from);
    brelse(to);
  }
}

static void
commit()
{
  if (log.lh.n > 0) {
    write_log();     // Write modified blocks from cache to log
    write_head();    // Write header to disk -- the real commit
    install_trans(); // Now install writes to home locations
    log.lh.n = 0;
    write_head();    // Erase the transaction from the log
  }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache with B_DIRTY.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b)
{
  int i;

  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  acquire(&log.lock);
  // if the block is already there in the log, then ignore
  // this in effect means that if multiple writes are done
  // to the same block, then the last one will actually be
  // written to the disks, others will get absorbed here!!
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // log absorbtion
      break;
  }

  // insert the blockno at the end of the block[] array 
  // if the block number is not found
  // else, this just rewrites block[i] in case of absorbtion
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n)
    log.lh.n++;
  b->flags |= B_DIRTY; // prevent eviction
  release(&log.lock);
}

