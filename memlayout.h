// Memory layout

/* Arjun
 * This file contains the boundaries, starting addresses which
 * contain physical as well as virtual addresses
 * of various "sections" that have been made in the memory
 *
 * EXTMEM
 * 	extended memory starts here, which is 1 MB
 * 	16^5, i.e. 2^20
 *
 * PHYSTOP
 * 	top of the physical memory, which is 224 MB
 * 	14 * 16^5, i.e. 224 * 2^20
 *
 * DEVSPACE
 * 	space for other devices, this is given as a physical
 * 	address directly
 * 	value is 4064 MB
 *		  15 * 2^28 + PHYSTOP
 *		= 15 * 2^8 * 2^20 + 224 MB
 *		= 15 * 256 * 1 MB + 224 MB
 *		= 3840 MB + 224 MB
 *		= 4064 MB
 *
 * KERNBASE
 * 	first virtual address of the kernel, which is 2 GB
 * 	8 * 16^7. i.e. 8 * 2^28, i.e. 2 * 2^30
 *
 * KERNLINK
 * 	this is basically KERNBASE + 1 MB
 * 	if you check the ELF file of the kernel, then you
 * 	can observe that the .text section starts at the
 * 	virtual address 0x80100000
 *
 * 	these instructions, to link at a particular address
 * 	are given in the kernel.ld file
 *
 * 	this is the address where the kernel is linked,
 * 	instructions are given to the linker in kernel.ld
 */

#define EXTMEM  0x100000            // Start of extended memory
#define PHYSTOP 0xE000000           // Top physical memory
#define DEVSPACE 0xFE000000         // Other devices are at high addresses

// Key addresses for address space layout (see kmap in vm.c for layout)
#define KERNBASE 0x80000000         // First kernel virtual address
#define KERNLINK (KERNBASE+EXTMEM)  // Address where kernel is linked

/* these macros perform address translation, from
 * physical to virtual and vice versa
 *
 * V2P(a)
 * 	subtracts KERNBASE from a
 *
 * P2V(a)
 * 	casts a as char *, then void * (not so sure why)
 * 	and then adds KERNBASE to the result
 *
 * Q: What difference does the cast make?
 */
#define V2P(a) (((uint) (a)) - KERNBASE)
#define P2V(a) ((void *)(((char *) (a)) + KERNBASE))

#define V2P_WO(x) ((x) - KERNBASE)    // same as V2P, but without casts
#define P2V_WO(x) ((x) + KERNBASE)    // same as P2V, but without casts
