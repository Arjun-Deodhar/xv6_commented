// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);

/* this is the end of the .bss section of kernel, 
 * i.e. the end of kernel stack 
 * 10a520 is the address of end
 */
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

/* used to link the pages in the free space list together
 * essential for creating a singly linked list 
 */
struct run {
  struct run *next;
};

/* Arjun
 * global data structure which essentially includes the free
 * space list head maintained by the kernel
 *
 * freelist is the head of an SLL (singly linked list) which
 * contains pointers to the first 4 bytes of each page in memory
 *
 * since kmem is a global variable, freelist will be initialised
 * to NULL
 */
struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.

/* Arjun
 * kinit1() adds all the pages lying between vstart and vend
 * to the free space list freelist, residing in the
 * global data structure kmem
 *
 * Q: What is the difference between kinit1() and kinit2()?
 *
 */
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

/* Arjun
 * kinit2() adds all the pages lying between vstart and vend
 * to the free space list freelist, residing in the
 * global data structure kmem
 */
void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

/* Arjun
 * freerange() calls kfree() for all the pages that lie
 * between virtaul addresses vstart and vend
 */
void
freerange(void *vstart, void *vend)
{
  char *p;

  /* p points to the rounded up address that corresponds to vstart
   */
  p = (char*)PGROUNDUP((uint)vstart);

  /* free each page between p and vend
   */
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)


/* Arjun
 * kfree() frees the page pointed at by v, which is added
 * to the free space list freelist of global data structure kmem
 * freelist is a linked list, and kfree() just adds a pointer
 * to page p (a run * pointer) to the head of the list
 */
void
kfree(char *v)
{
  struct run *r;

  /* error handling
   *
   * (uint)v % PGSIZE
   * 	checks if v is on a page boundary
   * 	== 0	good, on a page boundary
   * 	!= 0	not on a page boundary
   *
   * v < end
   * 	the virtual address specified is less than the value
   * 	of end[], i.e. the first address after kernel is loaded
   *
   * V2P(v) >= PHYSTOP
   * 	checks if the physical address that v translates to is 
   * 	less than the end of physical memory
   *
   * 	== 0	good, it is less
   * 	== 1	bad, accessing memory outside physical memory boundary
   */
  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  /* Q: How is this used to catch dangling refs?
   */
  memset(v, 1, PGSIZE);

  /* acquire(), release() just ensure that no other kernel
   * process changes the kmem data structure concurrently
   */
  if(kmem.use_lock)
    acquire(&kmem.lock);

  /* make r point to where v points to, by casting v
   * into a struct run *
   *
   * this essentialy implies that the first 4 bytes of any page
   * is used as a next pointer to the next free frame in the
   * free space list
   *
   * Q: is this interpretation of first 4 bytes being used for free space
   * list management correct?
   * Q: How does the kernel ensure that these 4 bytes are not
   * modified by the user process? Is the offset 0 of the page mapped
   * to the 5th byte?
   */
  r = (struct run*)v;

  /* now, insert the pointer r at the head of freelist
   * the head is basically kmem.freelist
   *
   * make r->next point to where the current head points to
   * make the head point to r
   */
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

/* Arjun
 * kalloc() will simply return the first free page that is
 * pointed to by the freelist in kmem
 * 
 * if kmem is NULL, then NULL will be returned, indicating that
 * there is no free space
 * 
 * as a result of kalloc(), the page allocated will be removed
 * from the free space list
 */
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);

  /* r points to the head of the free space list, 
   * i.e. kmem.freelist
   */
  r = kmem.freelist;
  /* if r != NULL, make freelist point to the next
   * node in the list, essentially removing the node
   * pointed at by r from the list
   */
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  /* cast r into a char * and return r
   */
  return (char*)r;
}
