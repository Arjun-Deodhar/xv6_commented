//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

/* Arjun
 * This is the topmost layer of the file system, containing the 
 * implementation of the system calls sys_open(), sys_read() etc
 *
 * the userland code will jump into here as a part of trap handling
 * of open(), read() etc
 *
 * this calls functions in the inode and / or file descriptor layer
 * depending on the requirement
 */

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.

/* argfd() returns the nth 32-bit syscall argument as a file descriptor
 * returns -1 if any error(s) occur
 * returns 0 to indicate success
 */
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  /* argint() will fetch the nth word-size syscall argument 
   * into variable fd, as an integer
   *
   * then, error handling is done to ensure that
   * 	fd >= 0
   * 	fd < NOFILE (less than the number of open files)
   * 	the value of ofile[fd] is not NULL
   *
   * then, the value of pfd is copied into *pfd
   * and the ofile[fd] struct file pointer is copied into *pf
   * these assignments are done only if pfd and pf are not NULL
   *
   * function returns 0 to indicate success
   */
  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.

/* fdalloc() returns the minimum available file desciptor
 *
 * i.e. the index of the first file pointer in the 
 * ofile[] array which is NULL (0)
 *
 * returns -1 if the maximum number of open files has been 
 * reached
 */
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *curproc = myproc();

  /* for each file pointer in ofile[]
   * 	if ofile[fd] is NULL
   * 		return the file pointer
   * return -1 if no file pointer is NULL
   */
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd] == 0){
      curproc->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

/* sys_dup() calls filedup()
 *
 * after checking the following
 * 	argv[0] is a file descriptor (valid)
 *
 *	fdalloc() is successful, so that fd is obtained
 *
 * then, filedup() is called for f
 */
int
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

/* sys_read() calls fileread()
 *
 * after checking the following
 * 	argv[0] is a file descriptor (valid)
 * 	argv[1] is a pointer to a buffer
 * 	argv[2] is an integer
 *
 * then, fileread() is called with the arguments obtained
 * from calling argfd(), argptr() and argint()
 *
 * The ||s in the if() statement ensure that all 3 functions are
 * called, thus setting up the variables in sys_read() properly :)
 * a little detail is that argint() is called for the 2nd argument
 * before argptr() is called for the first, because argptr() requires
 * the variable n to be set up, which is done in argint()
 * :))
 */
int
sys_read(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return fileread(f, p, n);
}

/* sys_write() calls filewrite()
 *
 * after checking the following
 * 	argv[0] is a file descriptor (valid)
 * 	argv[1] is a pointer to a buffer
 * 	argv[2] is an integer
 *
 * then, filewrite() is called with the arguments obtained
 * from calling argfd(), argptr() and argint()
 */
int
sys_write(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return filewrite(f, p, n);
}

/* sys_close() calls fileclose()
 *
 * after checking the following
 * 	argv[0] is a file descriptor (valid)
 *
 * ofile[fd] is set to NULL
 *
 * then, fileclose() is called with the argument obtained
 * from calling argfd()
 */
int
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

/* sys_stat() calls filestat()
 *
 * after checking the following
 * 	argv[0] is a file descriptor (valid)
 * 	argv[1] is a pointer pointing to sizeof(st) bytes
 *
 * then, filestat() is called with the argument obtained
 * from calling argfd() and argptr()
 */
int
sys_fstat(void)
{
  struct file *f;
  struct stat *st;

  if(argfd(0, 0, &f) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
/* link(src_name, tgt_name) links a file to a new name in the file
 * system directory structure
 * basically, it creates an additional directory  entry for an
 * existing inode :)
 */
int
sys_link(void)
{
  char name[DIRSIZ], *new, *old;
  struct inode *dp, *ip;

  if(argstr(0, &old) < 0 || argstr(1, &new) < 0)
    return -1;

  begin_op();
  // obtain old file's (src_name's) inode using namei()
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  // lock old inode
  ilock(ip);

  // if it is a directory, then return since that is not allowed in xv6
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  // increment link count by 1
  ip->nlink++;

  // write it back to disk and unlock the inode
  /* this unlock is necessary to avoid a deadlock scenario :)
   * process A: link("a/b/c/d", "e/f/g");
   * process B: link("e/f", "a/b/c/d/ee");
   *
   * OR a single process can deadlock ITSELF :)
   * link("a/b/c", "a/b/c/d")
   */
  iupdate(ip);
  iunlock(ip);

  // we now need to add an entry in the parent directory of target
  // pathname (new)
  // first obtain inode dp to parent directory
  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);

  // if dp->dev and ip->dev are different (on two devices) then this
  // link should not be allowed
  // call dirlink() to create the new directory entry
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

  // undo the changes that we did earlier
bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

//PAGEBREAK!
// unlink(pathname) is used to remove a directory entry for a file
int
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], *path;
  uint off;

  if(argstr(0, &path) < 0)
    return -1;

  begin_op();
  // obtain the parent directory's inode dp
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  // lookup the name in directory dp to obtain inode of the file being unlinked
  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  // number of links to this entry must be atleast 1
  if(ip->nlink < 1)
    panic("unlink: nlink < 1");

  // if the type of the inode is a directory, then a directory cannot be unlinked
  // if it is not empty (this is where rmdir fails for non-empty directory)
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  // set the directory entry to 0 and write the inode to disk
  memset(&de, 0, sizeof(de));
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");

  // if the file to be unlinked is a directory, then reduce the link count
  // and update the parent directory inode, since the ".." in the dir to be
  // removed will no longer "point" to the parent directory
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  // finally, decrement the reference count to the inode being unlinked,
  // and if the count AND link count reaches zero, we need to free the 
  // inode, thus freeing all the data blocks of that file
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

  // unlock dp and return -1
bad:
  iunlockput(dp);
  end_op();
  return -1;
}

/* create a file at path name path, where the file type is type
 * on the device which corresponds to the major and minor device numbers 
 *
 * calls lower level functions to create entries in the directory files
 * which need to be changed
 */
static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  // get a pointer to the parent directory, ensure that it exists
  if((dp = nameiparent(path, name)) == 0)
    return 0;

  // lock the parent directory
  ilock(dp);

  // find the file name in the directory dp, and get its
  // inode if that file exists
  if((ip = dirlookup(dp, name, 0)) != 0){
	// if the file already exists, then unlock the parent directory
    iunlockput(dp);
    ilock(ip);
	/* check if the types given vs that of actual file are both
	 * T_FILE
	 * in that case, return ip
	 * else, unlock the inode and return 0, since it is not possible
	 * to create that file here, since there is a file existing with a
	 * different file type
	 */
    if(type == T_FILE && ip->type == T_FILE)
      return ip;
    iunlockput(ip);
    return 0;
  }

  // we reached here means that anew file needs to be created
  // allocate an inode for that file
  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  // set the major and minor device numbers
  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  // write that inode to disk
  iupdate(ip);

  /* if a directory is created using create(), then d
   * increment dp->nlink, since the new directory will contain a
   * ".." entry that refers to the parent directory!
   * update the parent directory inode (write to disk)
   *
   * now, dirlink() ".", ".." to the newly allocated inode
   */
  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  // finally create an entry in the parent directory
  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  // unlock the parent directory inode
  iunlockput(dp);

  return ip;
}

/* sys_open()
 */
int
sys_open(void)
{
  char *path;
  int fd, omode;
  struct file *f;
  struct inode *ip;

  /* fetch the following arguments from the user stack
   *
   * 0	path of the file
   * 1	mode in which the file is to be opened
   */
  if(argstr(0, &path) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  /* O_CREATE is set means that the file is to be created
   *
   * create() is called, which returns an inode pointer to the
   * newly created file, if create() succeeds
   *
   * if ip is NULL, this means that create failed
   */
  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  /* filealloc() reserves an entry in the global file table for pointing to 
   * this file, and fdalloc() sets a file descriptor for the process to 
   * access this file via the open file array in struct proc
   *
   * effectively, if both calls succeed:
   * ofile[fd] in struct proc points to a newly created entry in the file table
   *
   * inode allocated for the file is unlocked and written to disk
   */
  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  end_op();

  /* set the entries in file table appropriately
   * f->ip will point to the inode of that file
   * f->off will be 0, since the file is just opened
   * readable and writable flags will be set according to the mode
   * flags specified by the system call
   */
  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;
}

/* just calls create() and tells it to create a directory entry in the
 * path, and return the inode of that entry
 *
 * begin_op() and end_op() are required since create() calls iupdate()
 * which internally calls log_write() to write an inode to disk
 */
int
sys_mkdir(void)
{
  char *path;
  struct inode *ip;

  begin_op();
  if(argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

/* just calls create(), similar to sys_mkdir()
 */
int
sys_mknod(void)
{
  struct inode *ip;
  char *path;
  int major, minor;

  begin_op();
  if((argstr(0, &path)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEV, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_chdir(void)
{
  char *path;
  struct inode *ip;
  struct proc *curproc = myproc();
  
  begin_op();
  // use namei() to parse the name and obtain the inode to the
  if(argstr(0, &path) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // ensure that the target is a directory
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);

  // iput() the current cwd, which basically writes it to disk
  iput(curproc->cwd);
  end_op();
  // set curproc->cwd to point to path's inode
  curproc->cwd = ip;
  return 0;
}

int
sys_exec(void)
{
  char *path, *argv[MAXARG];
  int i;
  uint uargv, uarg;

  if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      return -1;
    if(fetchint(uargv+4*i, (int*)&uarg) < 0)
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    if(fetchstr(uarg, &argv[i]) < 0)
      return -1;
  }
  return exec(path, argv);
}

int
sys_pipe(void)
{
  int *fd;
  struct file *rf, *wf;
  int fd0, fd1;

  if(argptr(0, (void*)&fd, 2*sizeof(fd[0])) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      myproc()->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  fd[0] = fd0;
  fd[1] = fd1;
  return 0;
}
