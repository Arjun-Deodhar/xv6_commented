// On-disk file system format.
// Both the kernel and user programs use this header file.

#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size

/* this is the xv6 file system's disk layout, which contains
 *
 * 	boot block	for the bootloader (if it exists)
 * 	sb block	superblock, containing metadata about the ENTIRE fs
 * 	log		blocks for logging 
 * 	inode blocks	blocks containing the inodes for the files and folders
 * 	free bit map	blocks for the bit map of the data blocks
 * 	data blocks	blocks containing actual data in files and folders
 */
// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};

/* NDIRECT
 * 	number of blocks whose addresses can be directly assigned into the inode
 *
 * NINDIRECT
 * 	number of blocks whose addresses have to filled into the indirect block, whose
 * 	address is given in addrs[NDIRECT]
 *
 * MAXFILE
 * 	essentially the maximum size of a file, in terms of number of blocks
 * 	for the original values of the macros, it is 140
 *
 * Q: does this mean that the maximum size of a file on xv6 is 
 * NDIRECT * BSIZE?
 * A: No, since the last entry in addrs[] refers to an indirection, i.e.
 * a block containing block addresses of the data blocks of the file
 * 
 * Q: Does this mean that xv6 has UFS style one level indirection? :)
 *
 * Q: Then, is the maximum size of a file on xv6 (NDIRECT + (BSIZE / uint)) blocks?
 * i.e. With the standard macros, this will be 
 * 	= 12 + (512 / 4)	blocks
 * 	= 12 + 128		blocks
 * 	= 140 			blocks
 * 	= 140 * 512 		bytes
 * 	= 71680			bytes
 *
 * A: YESS it is given in the macro above :))
 *
 * Q: Number of files in a directory in xv6?
 * will it be (140 * BSIZE) / sizeof(struct dirent))
 * where sizeof(struct dirent) is 16 bytes?
 * i.e. for BSIZE = 512, this turns out to be 4480, including . and ..
 */
#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) (b/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

/* dirent is a directory entry, that contains
 * 	inum	inode number of the file
 * 	name	name of the file, of strlen(name) is DIRSIZ - 1
 */
struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

