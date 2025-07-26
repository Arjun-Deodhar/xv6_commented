// Boot loader.
//
// Part of the boot block, along with bootasm.S, which calls bootmain().
// bootasm.S has put the processor into protected 32-bit mode.
// bootmain() loads an ELF kernel image from the disk starting at
// sector 1 and then jumps to the kernel entry routine.

#include "types.h"
#include "elf.h"
#include "x86.h"
#include "memlayout.h"

#define SECTSIZE  512

void readseg(uchar*, uint, uint);

/* Arjun
 * once the bootloader is done entering protected mode
 * and setting up the required segments, control jumps to
 * bootmain()
 *
 * this will perform the task of loading the kernel into RAM,
 * by reading blocks from the disk
 *
 * will understand the reading of the disk more clearly once
 * disk I/O is covered
 */
void
bootmain(void)
{
  /* Arjun
   *
   * struct elfhdr *elf
   * 	physical address where elf header will be loaded
   * 	this is for verifying whether it is correct
   * 	this space is just "scratch space"
   * 	(maybe, not so sure as of now)
   * 
   * struct proghdr *ph, *eph
   * 	pointers to program header strutures inside
   * 	the elf file
   * 	ph initially points to the start of the program header
   * 	table, and then it traverses the table
   *
   * 	eph points to the end of the program header table
   *
   * void (*entry)(void)
   * function pointer to a function that takes no args,
   * and returns nothing (void)
   *
   * uchar *pa is the physcal address at which the kernel
   * will be loaded (maybe)
   */
  struct elfhdr *elf;
  struct proghdr *ph, *eph;
  void (*entry)(void);
  uchar* pa;

  elf = (struct elfhdr*)0x10000;  // scratch space

  // Read 1st page off disk
  
  /* this is the elf header, which will contain
   * necessary data related to the elf file
   *
   * it will be loaded at physical address "elf"
   */
  readseg((uchar*)elf, 4096, 0);

  /* each elf has a MAGIC number in specific bytes
   * to indicate that the file is of correct format
   * this if statement checks if the magic value is
   * there in the elf file
   */
  
  // Is this an ELF executable?
  if(elf->magic != ELF_MAGIC)
    return;  // let bootasm.S handle error

  // Load each program segment (ignores ph flags).
  
  /* get the address of the program header table,
   * by adding phoff to elf
   * now, each program header can be accessed by
   * incrementing ph, since ph is of type
   * struct proghdr
   *
   * eph is the end addres of program header table
   * it is obtained by adding elf->phnum to ph
   * pointer arithmetic ensures that the increment
   * happens properly :)
   */
  ph = (struct proghdr*)((uchar*)elf + elf->phoff);
  eph = ph + elf->phnum;

  /* for each program header, 
   *
   * 	basically read the segment from disk, essentialy
   * 	loading it into memory
   * 	locate the segment using attributes of ph
   *
   * 	read segment of size ph->filesz from offset
   * 	in the file ph->off, at physical address pa
   */
  for(; ph < eph; ph++){
    pa = (uchar*)ph->paddr;
    readseg(pa, ph->filesz, ph->off);
    /* what if ph->memsz < filesz)
     */
    if(ph->memsz > ph->filesz)
      stosb(pa + ph->filesz, 0, ph->memsz - ph->filesz);
  }

  // Call the entry point from the ELF header.
  // Does not return!
  entry = (void(*)(void))(elf->entry);
  entry();
}

void
waitdisk(void)
{
  // Wait for disk ready.
  while((inb(0x1F7) & 0xC0) != 0x40)
    ;
}

// Read a single sector at offset into dst.
void
readsect(void *dst, uint offset)
{
  // Issue command.
  waitdisk();
  outb(0x1F2, 1);   // count = 1
  outb(0x1F3, offset);
  outb(0x1F4, offset >> 8);
  outb(0x1F5, offset >> 16);
  outb(0x1F6, (offset >> 24) | 0xE0);
  outb(0x1F7, 0x20);  // cmd 0x20 - read sectors

  // Read data.
  waitdisk();
  insl(0x1F0, dst, SECTSIZE/4);
}

// Read 'count' bytes at 'offset' from kernel into physical address 'pa'.
// Might copy more than asked.
void
readseg(uchar* pa, uint count, uint offset)
{
  uchar* epa;

  epa = pa + count;

  // Round down to sector boundary.
  pa -= offset % SECTSIZE;

  // Translate from bytes to sectors; kernel starts at sector 1.
  offset = (offset / SECTSIZE) + 1;

  // If this is too slow, we could read lots of sectors at a time.
  // We'd write more to memory than asked, but it doesn't matter --
  // we load in increasing order.
  for(; pa < epa; pa += SECTSIZE, offset++)
    readsect(pa, offset);
}
