#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat xv6_stat  // avoid clash with host struct stat
#include "types.h"
#include "fs.h"
#include "stat.h"
#include "param.h"

/* Arjun
 *
 * mkfs.c is a very interesting file
 * It contains a C program, whose executable will run BEFORE the kernel is
 * even compiled! (because of the Makefile)
 *
 * Hence, all the functions used by this program like lseek(), memmove()
 * etc are from the standard C library. It does use type definitions from fs.h,
 * like struct dinode, struct superblock etc...
 *
 * This is also why this program has its own main() function, since it is host
 * machine land code :)
 *
 * this program creates a file system that contains all the user land
 * code of xv6 along with some files like README and enters everything in the 
 * disk image with name specified in argv[1], (which is fs.img in the Makefile)
 *
 * the list of files to be entered into the file system should be given by name
 * as argv[2], argv[3], ..., argv[argc - 1]
 */

#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

#define NINODES 200

// Disk layout:
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]

/* here are some variables that are frequently required for block arithmetic
 *
 * nbitmap
 * 	number of blocks required to store the bitmap of the file system
 * 	FSSIZE is the size of the file system in blocks (defined in fs.h)
 * 	FSSIZE / 8 will be the number of bits required to store the bitmap
 *
 * 	this number divided by BSIZE will be number of blocks required to
 * 	store these many bits
 *
 * 	1 is added since we want the ceil() of the division
 *
 * ninodeblocks
 * 	number of blocks required to store the inodes of the file system
 * 	NINODES / IPB gives the number of blocks required, and 1 is added to
 * 	get the ceil() of the division
 *
 * 	IPB is the number of dinode structs that fit into one block,
 * 	which is defined as BSIZE / sizeof(struct dinode) in fs.h
 *
 * 	NINODES is 200
 *
 * nlog
 * 	number of blocks required for logging
 * 	this is typically (MAXOPBLOCKS * 3), which is 30 (original value)
 *
 * Q: What is the use of these logging blocks
 * Q: Why do we need MAXOPBLOCKS * 3 of these
 *
 * nmeta
 * 	number of blocks required to store the metadata of the file system
 * 	this is computed in main()
 *
 * nblocks
 * 	number of data blocks in the file system
 * 	calculated as the file system's size - nmeta, in main()
 */
int nbitmap = FSSIZE/(BSIZE*8) + 1;
int ninodeblocks = NINODES / IPB + 1;
int nlog = LOGSIZE;
int nmeta;    // Number of meta blocks (boot, sb, nlog, inode, bitmap)
int nblocks;  // Number of data blocks

/* fsfd is the file descriptor obtained on opening the file system image file
 * whose name is given as argv[1]
 *
 * sb is a global superblock structure
 */
int fsfd;
struct superblock sb;

/* zeroes[] is an array of BSIZE bytes, initialised to all zeros since
 * it is declared as a global variable
 *
 * this is typically used for writing zeroes into sectors of the disk
 * before creating a file system in it
 * Q: Why is this done?
 * For erasing earlier data that may conflict with / misguide the new fs?
 *
 * freeinode is the number of the current free inode, which is initialised to 1
 * Each time ialloc() is called, freeinode is incremented by 1
 *
 * freeblock is the number of allocated data blocks, i.e. the number of
 * the next block that is free
 */
char zeroes[BSIZE];
uint freeinode = 1;
uint freeblock;


void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);

/* this probably has something to do with endian-ness
 */
// convert to intel byte order
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}
// Byte-wise reverse
uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i, cc, fd;
  uint rootino, inum, off;
  struct dirent de;
  char buf[BSIZE];
  struct dinode din;


  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if(argc < 2){
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);
  }

  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  /* open() the argv[1] file, which is the disk into which the file
   * system is to be implemented
   */
  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0){
    perror(argv[1]);
    exit(1);
  }

  // 1 fs block = 1 disk sector
  /* now, some calculations are done, and the superblock 
   * is set up, by writing into the img
   *
   * [ boot block | sb block | log | inode blocks | free bit map | data blocks ]
   *
   * nmeta is the end of the metadata, i.e. the number of
   * blocks that the metadata will consist of
   * hence, later on freeblock = nmeta is done, in which 
   * freeblock is basically the first free block for storing data
   * of the actual files
   *
   * nmeta is computed as
   * 	2 		one for bootblock, one for superblock
   *  + nlog		number of blocks for logging
   *  + ninodeblocks	number of blocks for storing inodes
   *  + nbitmap		number of blocks for the bitmap
   *
   * nblocks is calculated as the file system's size - nmeta
   *
   */
  nmeta = 2 + nlog + ninodeblocks + nbitmap;
  nblocks = FSSIZE - nmeta;

  /* set the parameters of the superblock, and write the
   * superblock into the img
   *
   * the printf() will print on the host's stdout :)
   */
  sb.size = xint(FSSIZE);
  sb.nblocks = xint(nblocks);
  sb.ninodes = xint(NINODES);
  sb.nlog = xint(nlog);
  sb.logstart = xint(2);
  sb.inodestart = xint(2+nlog);
  sb.bmapstart = xint(2+nlog+ninodeblocks);

  printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE);

  freeblock = nmeta;     // the first free block that we can allocate

  /* write zeros into all the sectors of the file system
   *
   * wsect() basically writes the contents of buf
   * into sector i of the file
   *
   * since i is incremented FSSIZE number of times, it essentially
   * iterates over each sector
   */
  for(i = 0; i < FSSIZE; i++)
    wsect(i, zeroes);

  /* this is the C library memset() that will be called over here
   *
   * it will set the contents of buf to zeroes
   * then, memmove() will be called, which mves sizeof(sb) bytes
   * from sb into buf
   *
   * sb is a global variable containing the superblock for the fs, which was setup
   * earlier
   *
   * then, wsect() is called for writing the superblock at sector 1
   */
  memset(buf, 0, sizeof(buf));
  memmove(buf, &sb, sizeof(sb));
  wsect(1, buf);

  /* allocate an inode for the root directory, where T_DIR is the type
   * of the file to which the inode points to
   *
   * T_DIR means that the inode points to a file of type "directory"
   *
   * then, we assert() that the value returned by ialloc() returns 1,
   * which is ROOTINO
   */
  rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);

  /* now we will setup the directory entry of '.'
   *
   * bzero() fills '\0' into &de
   * de.inum is set to rootino
   * de.name is set to ".", which is for the current directory
   *
   * iappend() is then called, which appends sizeof(de) bytes to
   * file with inode number inum, from address &de
   *
   * After this, similar procedure is done for ".."
   */
  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  /* for each file with names from argv[2] to argv[argc - 1]
   * 	assert() that the name does not contain '/' in it
   *
   * 	open() the file
   * 	read it block by block into the img file, by calling
   * 	ialloc(), iappend() and required functions
   */
  for(i = 2; i < argc; i++){

    /* char *index(const char *s, char c) 
     * returns a pointer to the first occurence of character c in string s
     *
     * if this pointer is NULL, it means that s does not contain c
     * this check is to assert() that the file name argv[i] does not contain
     * '/', which will cause problems in the pathname nomenclature
     *
     * then, the file is opened for reading
     */
    assert(index(argv[i], '/') == 0);

    if((fd = open(argv[i], 0)) < 0){
      perror(argv[i]);
      exit(1);
    }

    // Skip leading _ in name when writing to file system.
    // The binaries are named _rm, _cat, etc. to keep the
    // build operating system from trying to execute them
    // in place of system binaries like rm and cat.
    if(argv[i][0] == '_')
      ++argv[i];

    /* allocate an inode for the file, set the type of the file to
     * T_FILE, which stands for a regular file
     *
     * create a directory entry in inode number returned by ialloc()
     * with the name of the file as argv[i]
     *
     * call iappend() to add the entry to the root directory's inode
     *
     * then, read the argv[i] file block by block, 
     * call iappend() to append the blocks to the on-disk file
     */
    inum = ialloc(T_FILE);

    bzero(&de, sizeof(de));
    de.inum = xshort(inum);
    strncpy(de.name, argv[i], DIRSIZ);
    iappend(rootino, &de, sizeof(de));

    while((cc = read(fd, buf, sizeof(buf))) > 0)
      iappend(inum, buf, cc);

    close(fd);
  }

  // fix size of root inode dir
  /* we need to fix the size of the root directory
   * Q: Why is this really done?
   *
   * read the root directory inode into variable din
   * round up din.size to the smallest block boundary greater than din.size
   * write back the root directory inode onto the disk
   */
  rinode(rootino, &din);
  off = xint(din.size);
  off = ((off/BSIZE) + 1) * BSIZE;
  din.size = xint(off);
  winode(rootino, &din);

  /* balloc() will set up the data block bitmap, where freeblock number
   * of data blocks have been allocated as of now
   */
  balloc(freeblock);

  exit(0);
}

/* wsect() wrutes the contents inside buf into sector number sec
 * the contents are written into the open file obtained
 * by indexing the open file array by the file descriptor fsfd
 *
 * fsfd is a global, and hence if it should be set properly
 *
 * lseek() is used to go to the location sec * BSIZE, which is the
 * sector number at which we want to write the contents of buf
 *
 * the if statement is used to ensure that lseek() takes us to that location
 * properly
 *
 * then, write() is called in which we write BSIZE bytes from address buf into
 * fsfd, thus writing one sector
 *
 * error checking is done to ensure that those many bytes are actually written
 */
void
wsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE){
    perror("lseek");
    exit(1);
  }
  if(write(fsfd, buf, BSIZE) != BSIZE){
    perror("write");
    exit(1);
  }
}

/* winode() writes the contents of inode ip into the inode number
 * inum on disk
 *
 * initially, some arithmetic needs to be done in order to calculate
 * the exact sector in which the inode is to be written
 *
 * IBLOCK returns this value, i.e. the block in which the inode
 * is to be written
 *
 * this is calculated by adding (inum / IPB) to sp.inodestart
 *
 * However, there may be multiple inodes in a block, and since i/o with
 * the disk can be done in units of blocks, wsect() needs to be called
 *
 * Then, the sector bn is read into variable buf
 * Now, the inode struct lying at offset (inum % IPB) must be updated
 *
 * Then, *dip = *ip fills the contents of *ip into that offset
 * wsect() is called to write the sector back, with the updated inode
 *
 * (this can actually be done by lseek() to go to the exact byte, but
 * wsect() is called to maintain the convention :)
 * eventually, wsect() calls lseek())
 */
void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

/* read inode with number inum into inode struct pointed to by ip
 *
 * initially, arithmetic performed to obtain the sector number in 
 * which the inode exists
 *
 * the sector is read into buffer buf, and dip points to the 
 * (inum % IPB)th inode entry 
 *
 * then, *dip is copied into *ip
 *
 * read the comment before ialloc() for more details about the arithmetic
 */
void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

/* rsect() reads sector number sect into buffer buf
 *
 * lseek() will make the offset of fsfd point to  sec * BSIZE
 * error handling is done to ensure that lseek() works properly
 *
 * the sector is then read() into variable buf
 */
void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE){
    perror("lseek");
    exit(1);
  }
  if(read(fsfd, buf, BSIZE) != BSIZE){
    perror("read");
    exit(1);
  }
}

uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  /* bzero(void *s, size_t n)
   * erases the data in the n bytes of the memory starting at the
   * location pointed to by s, by writing zeros ('\0') to that area
   *
   * here, &din points to the disk inode structure and sizeof(din) is
   * 64 bytes
   *
   * the attributes set in din are:
   * 	type	set to type, the parameter passed to ialloc()
   * 	nlink	1 (since there is one link to this)
   * 	size	0, since the file is initially empty
   *
   * then, winode() is called to write the dinode into the disk
   * where inum is the inode number, which is the value of freeinode
   * ialloc() was called
   *
   * inum is then returned
   */
  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  winode(inum, &din);
  return inum;
}

/* balloc() creates the free bitmap for "used" number of data blocks
 * that have been allocated to files
 *
 * assert() is called to ensure that used is less than BSIZE * 8
 * Q: Why is this assert() necessary?
 *
 * buf is the free bitmap block, which is zeroed
 *
 * for each ith block number allocated (0 <= i < used)
 * 	buf[i / 8] gives the (i / 8)th byte to set, since
 * 	one byte suffices to store information about 8 blocks
 *
 * 	it is ORed with 0x1 shifted left (i % 8), i.e. the (i % 8)th
 * 	bit from the right is set
 *
 * wsect() is called to write the bitmap onto disk
 */
void
balloc(int used)
{
  uchar buf[BSIZE];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < BSIZE*8);
  bzero(buf, BSIZE);
  for(i = 0; i < used; i++){
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
  wsect(sb.bmapstart, buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

/* iappend() appends n bytes from address xp into the file whose
 * inode number is inum
 *
 * a check is made to ensure that the file contains no more than 
 * MAXFILE blocks
 */
void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint indirect[NINDIRECT];
  uint x;

  /* rinode() will read the inode with number inum into the
   * variable din
   *
   * off is initialised with the file's size
   */
  rinode(inum, &din);
  off = xint(din.size);
  // printf("append inum %d at off %d sz %d\n", inum, off, n);
 
  /* while the number of bytes to be read is greater than 0,
   *
   * get the block number to be appended to the file
   * assert() that is is less than the maximum number of blocks any file
   * can have on xv6
   *
   * obtain a block address for the block, and store the address in 
   * 	the addrs[] array if it can be stored directly
   *
   * 	the addrs[NDIRECT] block's proper entry, if it needs to be 
   * 	stored indirectly
   * 
   * set the value of x to the block allocated
   * 
   * update the values of
   * 	n	the number of bytes remaining to be appended
   * 	off	the updated size of the file
   */
  while(n > 0){
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);
    /* if the block can be stored directly, then the
     * file block number (fbn) will be less than NDIRECT
     *
     * in this case, we can directly store the value of freeblock
     * into din.addrs[fbn] and set x to it
     */
    if(fbn < NDIRECT) {
      if(xint(din.addrs[fbn]) == 0){
        din.addrs[fbn] = xint(freeblock++);
      }
      x = xint(din.addrs[fbn]);
    }
    /* else, the block needs to be stored indirectly
     * 
     * if it is the first one to be stored indirectly
     *		allocate a block for storing the addresses of the indirectly
     *		stored data blocks of the file
     *		put this block address into din.addrs[NDIRECT]
     * Now
     * read the block containing all the addresses of the indirect data blocks,
     * into buffer "indirect"
     *
     * if the indirect[fbn - NDIRECT]th block address entry is free
     * 		set this entry to freeblock, and increment freeblock
     *		write the sector back into the disk
     *
     * This check is necessary, since the last block of the file may not necessary 
     * be filled up to the block boundary, i.e. it may have less than BSIZE bytes
     * In this case, we do not need to update the indirect block on disk
     *
     * set the value of x to indirect[fbn - NDIRECT]
     */
    else {
      if(xint(din.addrs[NDIRECT]) == 0){
        din.addrs[NDIRECT] = xint(freeblock++);
      }
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      if(indirect[fbn - NDIRECT] == 0){
        indirect[fbn - NDIRECT] = xint(freeblock++);
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      }
      x = xint(indirect[fbn - NDIRECT]);
    }

    /* set n1 to the number of bytes to be written in this iteration
     *
     * rsect() reads sector number x, into buffer buf
     *
     * bcopy() will copy n1 bytes, at address p, to the destination
     * buf + some offset
     * this offset is off (the size of file in bytes) - (fbn * BSIZE)
     * fbn * BSIZE is the number of bytes till the current block number to be
     * appended to the file, (including the last block)
     *
     * then, the sector number x is written back into the disk, from buffer buf
     *
     * now, the following variables are updated
     * n 	decrease by n1, so that it contains the remaining bytes to write
     * off	increase by n1, so that it contains the updated size of the file
     * p	increase by n1, so that it points to the next byte to be written
     */
    n1 = min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * BSIZE), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  /* set the file size to the updated size
   * update the dinode structure by calling winode()
   */
  din.size = xint(off);
  winode(inum, &din);
}
