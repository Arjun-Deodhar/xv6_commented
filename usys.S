#include "syscall.h"
#include "traps.h"

/* Arjun: FILE COMPLETED
 * usys.S
 *
 * this file contains
 * 1. SYSCALL macro
 *	used to insert asm code for moving the syscall number
 *	into eax register, when the syscall will be called
 * 
 * 2. series of "calls" to the macros
 */

/*
 * SYSCALL macro creates assembly code for 
 * calling each syscall, by name (it inserts
 * the name as a label in the asm code)
 * 
 * it inserts a label with the name of the syscall
 * then it concatenates the name to SYS, and moves the
 * syscall number into eax register, so that the syscall
 * can be identified in the syscalls array by using the
 * eax register value as an index into the array
 *
 * int T_SYSCALL basically is int 64, since
 * T_SYSCALL is defined as 64 in traps.h
 */

#define SYSCALL(name) \
  .globl name; \
  name: \
    movl $SYS_ ## name, %eax; \
    int $T_SYSCALL; \
    ret

SYSCALL(fork)
SYSCALL(exit)
SYSCALL(wait)
SYSCALL(pipe)
SYSCALL(read)
SYSCALL(write)
SYSCALL(close)
SYSCALL(kill)
SYSCALL(exec)
SYSCALL(open)
SYSCALL(mknod)
SYSCALL(unlink)
SYSCALL(fstat)
SYSCALL(link)
SYSCALL(mkdir)
SYSCALL(chdir)
SYSCALL(dup)
SYSCALL(getpid)
SYSCALL(sbrk)
SYSCALL(sleep)
SYSCALL(uptime)
