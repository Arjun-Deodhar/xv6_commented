#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "syscall.h"

// User code makes a system call with INT T_SYSCALL.
// System call number in %eax.
// Arguments on the stack, from the user call to the C
// library system call function. The saved user %esp points
// to a saved program counter, and then the first argument.

// Fetch the int at addr from the current process.
int
fetchint(uint addr, int *ip)
{
  struct proc *curproc = myproc();

  /* ensure that the addr does not exceed the process' size
   */
  if(addr >= curproc->sz || addr+4 > curproc->sz)
    return -1;
  *ip = *(int*)(addr);
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Doesn't actually copy the string - just sets *pp to point at it.
// Returns length of string, not including nul.

/* comments are self explanatory
 *
 * length returned can be useful for error checking
 * -1 indicates that the string argument was not nul terminated
 */
int
fetchstr(uint addr, char **pp)
{
  char *s, *ep;
  struct proc *curproc = myproc();

  if(addr >= curproc->sz)
    return -1;
  *pp = (char*)addr;
  ep = (char*)curproc->sz;
  for(s = *pp; s < ep; s++){
    if(*s == 0)
      return s - *pp;
  }
  return -1;
}

// Fetch the nth 32-bit system call argument.
int
argint(int n, int *ip)
{
  /* fetchint() is wisely called, for address 
   *
   * 	myproc()->tf->esp	esp value in the trap frame
   * 				this is the value of esp after the syscall
   * 				was made
   *
   * 	+ 4			for the return address pushed on user stack
   * 				while calling syscall
   *
   * 	+ 4 * n			skips the first n 32-bit arguments
   */
  return fetchint((myproc()->tf->esp) + 4 + 4*n, ip);
}

// Fetch the nth word-sized system call argument as a pointer
// to a block of memory of size bytes.  Check that the pointer
// lies within the process address space.

/* argptr() is used to fetch the nth system call argument, but in this
 * case the argument itself is a pointer to the actual argument, which is
 * somewhere in memory
 *
 * argint() is called to fetch that pointer into integer i
 * then, that is copied as a char * into pp
 *
 * As a result, pp points to the pointer which points to the argument
 * (pp is a double pointer :))
 */
int
argptr(int n, char **pp, int size)
{
  int i;
  struct proc *curproc = myproc();
 
  if(argint(n, &i) < 0)
    return -1;
  if(size < 0 || (uint)i >= curproc->sz || (uint)i+size > curproc->sz)
    return -1;
  *pp = (char*)i;
  return 0;
}

// Fetch the nth word-sized system call argument as a string pointer.
// Check that the pointer is valid and the string is nul-terminated.
// (There is no shared writable memory, so the string can't change
// between this check and being used by the kernel.)

/* argstr() is used to fetch the nth system call argument, but in this
 * case the argument itself is a pointer to the actual argument, which is
 * somewhere in memory, in the form of a string (nul terminated sequence of
 * characters / bytes)
 *
 * argint() is called to fetch that pointer into addr
 * then, a pointer to that string is obtained by fetchstr(), 
 * which returns the length of the string excluding the nul byte
 *
 * as a result, *pp now points to the first byte of that string
 * if fetchstr() is successful (which it will be if the string argument
 * is proper)
 */
int
argstr(int n, char **pp)
{
  int addr;
  if(argint(n, &addr) < 0)
    return -1;
  return fetchstr(addr, pp);
}

/* Arjun
 * 
 * function definitions of all system calls in xv6
 * adding a new syscall means inserting its
 * prototype over here, so that it can be externally visible
 */
extern int sys_chdir(void);
extern int sys_close(void);
extern int sys_dup(void);
extern int sys_exec(void);
extern int sys_exit(void);
extern int sys_fork(void);
extern int sys_fstat(void);
extern int sys_getpid(void);
extern int sys_kill(void);
extern int sys_link(void);
extern int sys_mkdir(void);
extern int sys_mknod(void);
extern int sys_open(void);
extern int sys_pipe(void);
extern int sys_read(void);
extern int sys_sbrk(void);
extern int sys_sleep(void);
extern int sys_unlink(void);
extern int sys_wait(void);
extern int sys_write(void);
extern int sys_uptime(void);

/* Arjun
 * array of function pointers to each syscall
 *
 * [] indicates the index of the function pointer
 * which contains some macro defined in syscall.h
 */
static int (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
};

void
syscall(void)
{
  int num;
  struct proc *curproc = myproc();
 
  /* Arjun
   *
   * tf->eax will basically contain the eax register value
   * in the trap frame tf, of current process curproc
   *
   * eax will contain the syscall number, which will be put
   * in eax by the macro SYSCALL(syscall_name) in usys.S
   * 
   * the if condition
   * if(num > 0 && num < NELEM(syscalls) && syscalls[num])
   * checks whether the syscall number num is in bounds or not
   * if it is within bounds, then call the function pointed to by
   * the function pointer in syscalls[] array
   *
   * else, report an error and set curproc->tf->eax to -1
   *
   * NELEM(syscalls) returns the number of syscalls in the syscall
   * array
   */
  num = curproc->tf->eax;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    curproc->tf->eax = syscalls[num]();
  } else {
    cprintf("%d %s: unknown sys call %d\n",
            curproc->pid, curproc->name, num);
    curproc->tf->eax = -1;
  }
}
