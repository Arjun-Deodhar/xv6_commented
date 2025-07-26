//
// File descriptors
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"

/* Arjun
 * This file contains the logical layer of the file system,
 * or the "file decriptor layer"
 *
 * This contains system functions that deal with files based on the struct
 * file, and call lower layer modules as per the requirement
 *
 * devsw[NDEV]
 * 	array of devices, where each struct devsw contains
 * 	a pair of read() and write() function pointers
 *
 * ftable
 * 	system wide open file table
 */
struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

/* fileinit() calls initlock() on the lock of ftable
 */
void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
/*
 * filealloc() allocates a file structure from ftable
 * returns a pointer to the structure, if an entry is free
 * returns NULL (0) if no entry is found
 */
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  /* for each entry in teh file table
   * 	if the reference count in the struct file is 0
   * 		set the reference count to 1
   * 		release() the lock on ftable
   *		return a pointer to that struct
   */
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  /* no entry was found
   * release the lock
   * return NULL (0)
   */
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
/* filedup() increments the reference count of struct file *f
 * and returns the pointer back
 */
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
/* fileclose() decrements the reference count of the file
 * if this reaches zero, then the entry in the file table is cleared
 * iput() is also called to "put" the inode back
 */
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  /* decrement the reference count
   *
   * if it is still greater than zero, then release() the lock
   * and return
   */
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }

  /* reference count reached zero
   *
   * set the f->ref to 0 (Does this really need to be done?)
   * set the type of the struct file entry to FD_NONE
   * release() the lock on the file table
   */
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  /* if the file was of type FD_PIPE, then close the pipe
   * (not sure what this means as of now)
   *
   * this is to wakeup() readers!!
   * WOW!!
   *
   * if it was an inode type entry
   * 	call iput() to put the inode back
   * 	enclose this operation between begin_op() and end_op()
   * 	for atomicity, using the logging layer
   */
  if(ff.type == FD_PIPE)
    pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
/* returns metadata about the file f
 * if the type of the ftable entry is FD_INODE
 * 	lock the inode f->ip
 * 	copy the state information into f->ip
 * 	unlock the inode f->ip
 * 	return 0
 */
int
filestat(struct file *f, struct stat *st)
{
  if(f->type == FD_INODE){
    ilock(f->ip);
    stati(f->ip, st);
    iunlock(f->ip);
    return 0;
  }
  return -1;
}

// Read from file f.
/* fileread() reads n bytes from file f into address addr
 * returns the number of bytes read
 *
 * this function calls the lower level inode function
 * readi() to actually read from the inode
 */
int
fileread(struct file *f, char *addr, int n)
{
  int r;

  /* ensure that the file is readable
   *
   * if the file is of type FD_PIPE
   * 	call piperead()
   *
   * if the file is of type FD_INODE
   * 	lock the inode
   * 	call readi()
   * 	increment the file offset by r, i.e.
   * 	the number of bytes successfully read
   * 	unlock the inode
   * 	return r
   *
   * if the type is something other than these two
   * 	panic()
   *
   * Q: What exactly is the FD_PIPE type?
   */
  if(f->readable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return piperead(f->pipe, addr, n);
  if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
    return r;
  }
  panic("fileread");
}

//PAGEBREAK!
// Write to file f.
int
filewrite(struct file *f, char *addr, int n)
{
  int r;

  if(f->writable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return pipewrite(f->pipe, addr, n);
  if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    return i == n ? n : -1;
  }
  panic("filewrite");
}
