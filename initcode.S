# Initial process execs /init.
# This code runs in user space.

#include "syscall.h"
#include "traps.h"

# Arjun
# this is the first line of user code that will run, once the kernel
# creates this process "by hand"
# this process just calls exec() for "/init"
# thus the implementation of init remains out of the xv6.img :)
# the code below just calls exec() with some argv setup

# exec(init, argv)
.globl start
start:
  pushl $argv
  pushl $init
  pushl $0  // where caller pc would be
  movl $SYS_exec, %eax
  int $T_SYSCALL

# for(;;) exit();
exit:
  movl $SYS_exit, %eax
  int $T_SYSCALL
  jmp exit

# char init[] = "/init\0";
init:
  .string "/init\0"

# char *argv[] = { init, 0 };
.p2align 2
argv:
  .long init
  .long 0

