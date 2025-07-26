# The xv6 kernel starts executing in this file. This file is linked with
# the kernel C code, so it can refer to kernel symbols such as main().
# The boot block (bootasm.S and bootmain.c) jumps to entry below.
        
# Multiboot header, for multiboot boot loaders like GNU Grub.
# http://www.gnu.org/software/grub/manual/multiboot/multiboot.html
#
# Using GRUB 2, you can boot xv6 from a file stored in a
# Linux file system by copying kernel or kernelmemfs to /boot
# and then adding this menu entry:
#
# menuentry "xv6" {
# 	insmod ext2
# 	set root='(hd0,msdos1)'
# 	set kernel='/boot/kernel'
# 	echo "Loading ${kernel}..."
# 	multiboot ${kernel} ${kernel}
# 	boot
# }

#include "asm.h"
#include "memlayout.h"
#include "mmu.h"
#include "param.h"

# Multiboot header.  Data to direct multiboot loader.
.p2align 2
.text
.globl multiboot_header
multiboot_header:
  #define magic 0x1badb002
  #define flags 0
  .long magic
  .long flags
  .long (-magic-flags)

# By convention, the _start symbol specifies the ELF entry point.
# Since we haven't set up virtual memory yet, our entry point is
# the physical address of 'entry' (0x8010000c)
.globl _start
_start = V2P_WO(entry)

# Entering xv6 on boot processor, with paging off.
.globl entry
entry:
  # Turn on page size extension for 4Mbyte pages
  
  # Arjun
  # set the CR4 bit on in the cpu
  # do not know as of now, what CR4 bit indicates
  # PSE stands for Page Size Extension
  movl    %cr4, %eax
  orl     $(CR4_PSE), %eax
  movl    %eax, %cr4

  # Set page directory

  # Arjun
  # CR3 enables the processor to locate the page
  # table directory for the current task, and entrypgdir
  # is the pointer to the page directory used by entry
  # address of entrypgdir is given in the ELF file
  movl    $(V2P_WO(entrypgdir)), %eax
  movl    %eax, %cr3

  # Turn on paging.
  movl    %cr0, %eax
  orl     $(CR0_PG|CR0_WP), %eax
  movl    %eax, %cr0

  # Set up the stack pointer.
  movl $(stack + KSTACKSIZE), %esp

  # Jump to main(), and switch to executing at
  # high addresses. The indirect call is needed because
  # the assembler produces a PC-relative instruction
  # for a direct jump.
  # using *%eax, we use an absolute jump, so that it
  # will jump to main(), irrespective of the location of
  # main()
  # 
  # if the kernel is to be made relocatable, then we will
  # need to jump to it even if it as ANY higher address
  # using *%eax ensures that this will happen without fail
  mov $main, %eax
  jmp *%eax

# Arjun
# .comm will allocate KSTACKSIZE bytes at location stack
# KSTACKSIZE is 4096
# why does this stack go at address 801144d0
# stack is allocated at the highest memory address of the
# kernel
.comm stack, KSTACKSIZE
