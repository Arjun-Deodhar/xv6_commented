// This file contains definitions for the
// x86 memory management unit (MMU).

// Eflags register
#define FL_IF           0x00000200      // Interrupt Enable

// Control Register flags
#define CR0_PE          0x00000001      // Protection Enable
#define CR0_WP          0x00010000      // Write Protect
#define CR0_PG          0x80000000      // Paging

#define CR4_PSE         0x00000010      // Page size extension

// various segment selectors.

/* there are four segment selectors in the segment
 * descriptor table
 *
 * KCODE, KDATA for kernel code and data
 * UCODE, UDATA for user's code and data
 */
#define SEG_KCODE 1  // kernel code
#define SEG_KDATA 2  // kernel data+stack
#define SEG_UCODE 3  // user code
#define SEG_UDATA 4  // user data+stack
#define SEG_TSS   5  // this process's task state

// cpu->gdt[NSEGS] holds the above segments.
#define NSEGS     6

#ifndef __ASSEMBLER__

/* Arjun
 * This is a segment descriptor structure, which is used to
 * store an entry in a segment table
 *
 * it contains the following entries
 * 
 * lim
 * 	limit of the segment (20 bits in total)
 *
 * base
 * 	base address of the segment (32 bits in total)
 *
 * dpl
 * 	descriptor privilege level
 * s
 *	system or user segment
 * p
 * 	present bit (present or not)
 */
// Segment Descriptor
struct segdesc {
  uint lim_15_0 : 16;  // Low bits of segment limit
  uint base_15_0 : 16; // Low bits of segment base address
  uint base_23_16 : 8; // Middle bits of segment base address
  uint type : 4;       // Segment type (see STS_ constants)
  uint s : 1;          // 0 = system, 1 = application
  uint dpl : 2;        // Descriptor Privilege Level
  uint p : 1;          // Present
  uint lim_19_16 : 4;  // High bits of segment limit
  uint avl : 1;        // Unused (available for software use)
  uint rsv1 : 1;       // Reserved
  uint db : 1;         // 0 = 16-bit segment, 1 = 32-bit segment
  uint g : 1;          // Granularity: limit scaled by 4K when set
  uint base_31_24 : 8; // High bits of segment base address
};

// Normal segment

/* Arjun
 *
 * the SEG macro creates a struct segdesc, with the
 * attributes derived from type, base, lim and dpl
 *
 * SEG16 is similar to SEG, the only change is that it sets
 * the db bit to 0, which marks it as a 16 - bit segment
 *
 * hardcoded attributes make it a
 * 1. system segment (only sys can use it)
 * 2. present bit is set
 * 3. not available for other usage
 * 4. not reserved
 * 5. it is a 32 bit segment
 * 6. it is granular, meaning that limit is in 4K units
 *
 * type	
 *
 * base	
 * 	base address
 * lim	
 * 	limit of the segment
 *
 * dpl
 * 	descriptor privilege level of the entry
 *
 * I have "unfolded" the macro so that it will be easier
 * to read
 *
 * struct segdesc {
 * ((lim) >> 12) & 0xffff	lower 16 bits of limit
 * (uint)(base) & 0xffff   	lower 16 bits of base	
 * ((uint)(base) >> 16) & 0xff	higher 8 bits of base
 * type				type of the segment
 * 1				system segment descriptor
 * dpl				privilege level	
 * 1      			it is present
 * (uint)(lim) >> 28		higher 4 bits of limit
 * 0 				not available for other usage
 * 0				not reserved
 * 1				it is a 32 bit segment
 * 1				it is granular (limit is in 4K units)
 * (uint)(base) >> 24		higher 8 bits of the base
 * }
 *
 */
#define SEG(type, base, lim, dpl) (struct segdesc)    \
{ ((lim) >> 12) & 0xffff, (uint)(base) & 0xffff,      \
  ((uint)(base) >> 16) & 0xff, type, 1, dpl, 1,       \
  (uint)(lim) >> 28, 0, 0, 1, 1, (uint)(base) >> 24 }
#define SEG16(type, base, lim, dpl) (struct segdesc)  \
{ (lim) & 0xffff, (uint)(base) & 0xffff,              \
  ((uint)(base) >> 16) & 0xff, type, 1, dpl, 1,       \
  (uint)(lim) >> 16, 0, 0, 1, 0, (uint)(base) >> 24 }
#endif

#define DPL_USER    0x3     // User DPL

// Application segment type bits
#define STA_X       0x8     // Executable segment
#define STA_W       0x2     // Writeable (non-executable segments)
#define STA_R       0x2     // Readable (executable segments)

// System segment type bits

/* Arjun
 * three types of descriptors, one code for each
 *
 * STS_T32A
 * 	available task segment selector
 *
 * STS_IG32
 * 	interrupt gate
 *
 * STS_TG32
 * 	trap gate
 *
 * these values are put in the descriptor in the
 * respective type field
 *
 * (as defined in 386 architecture)
 */
#define STS_T32A    0x9     // Available 32-bit TSS
#define STS_IG32    0xE     // 32-bit Interrupt Gate
#define STS_TG32    0xF     // 32-bit Trap Gate

/* Arjun
 *
 * paging in xv6 happens in two stages (for 4KB pages)
 *
 * 1. pgdir
 * page directory is accessed, which contains addresses of 
 * page tables (addresses of pages of page tables)
 * the part of the va (virtual address) that indexes into the
 * page directory is the first 10 bits of la
 *
 * 2. ptable
 * then, the page table is accessed. page table index part of
 * the virtual address indexes into this page table to get the
 * correct page table entry, to get the frame number
 *
 * after these steps, the offset can be added to the frame number
 * for obtaining the physical address, which is given to RAM
 */

// A virtual address 'la' has a three-part structure as follows:
//
// +--------10------+-------10-------+---------12----------+
// | Page Directory |   Page Table   | Offset within Page  |
// |      Index     |      Index     |                     |
// +----------------+----------------+---------------------+
//  \--- PDX(va) --/ \--- PTX(va) --/

// page directory index


/* Arjun
 *
 * These two macros return the index into the paging tables
 * so that we directly get the address of a PDE or a PTE
 *
 * PDX(va)
 * 	the page directory index lies in uppermost 10 bits
 * 	i.e. bits 31 - 22
 *
 * 	right shift va 22 times, so that the lowermost 10 bits
 * 	of va contain the desired index
 * 	since va is of 32 bits, the lowermost 10 bits need to be obtained
 * 	and the others must be zeroed
 *
 * 	Hence we need to mask with 0000 0000 ... 0011 1111 1111
 * 	i.e. 0x000003FF
 *
 * PTX(va)
 * 	the page table index lies in bits 21 - 12
 * 	apply same logic as PDX(va), but right shift 12 times
 *
 */
#define PDX(va)         (((uint)(va) >> PDXSHIFT) & 0x3FF)

// page table index
#define PTX(va)         (((uint)(va) >> PTXSHIFT) & 0x3FF)

// construct virtual address from indexes and offset
#define PGADDR(d, t, o) ((uint)((d) << PDXSHIFT | (t) << PTXSHIFT | (o)))

// Page directory and page table constants.
/* Arjun
 * various macros related to paging
 *
 * NPDENTRIES
 * 	number of entries in a page directory
 *
 * NPTENTRIES
 * 	number of entries in a page table
 *
 * PGSIZE
 * 	size of a page, default valus is 4KB
 *
 * PTXSHIFT
 * 	number of times we need to right shift the virtual
 * 	address so that we get the page table index in the
 * 	lowermost 10 bits of va
 *
 * PDXSHIFT
 * 	number of times we need to right shift the virtual
 * 	address so that we get the page directory index in the
 * 	lowermost 10 bits of va
 */
#define NPDENTRIES      1024    // # directory entries per page directory
#define NPTENTRIES      1024    // # PTEs per page table
#define PGSIZE          4096    // bytes mapped by a page

#define PTXSHIFT        12      // offset of PTX in a linear address
#define PDXSHIFT        22      // offset of PDX in a linear address

/* Arjun
 *
 * PGROUNDUP(sz) 
 * 	replaces sz by least number of bytes more than sz
 * 	that are an integral multiple of PGSIZE
 *
 * PGROUNDDOWN(sz)
 * 	replaces sz by the greatest number of bytes less than sz
 * 	that is an intergral multiple of PGSIZE
 *
 * for example, PGSIZE = 32
 * 	sz		PGROUNDUP(sz)	PGROUNDDOWN(sz)
 * 	33		64		32
 * 	150		160		128
 * 	145		160		128
 * 	200		224		192
 */
#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))

// Page table/directory entry flags.

/* Arjun
 * macros related to bits in the page table entry
 *
 * PTE_P
 * 	valid / invalid bit, 1 means valid
 *
 * PTE_W
 * 	page has write permissions when set
 *
 * PTE_U
 *	page is in user space
 *
 * PTE_PS
 * 	4MB or 4KB pages
 */
#define PTE_P           0x001   // Present
#define PTE_W           0x002   // Writeable
#define PTE_U           0x004   // User
#define PTE_PS          0x080   // Page Size

// Address in page table or page directory entry

/* Arjun
 * a page table entry contains flags in the lowermost 12 bits,
 * i.e. 11 - 0
 * the bits 31 - 12 contain the physical page number (PPN)
 *
 * PTE_ADDR(pte)
 * 	this returns the PPN, by applying the mask ~0xFFF
 * 	which will simply clear the flag bits and return the
 * 	page table entry
 * 	this is done so that the offset in va can be added to
 * 	PPN for obtaining the physical address to be given to RAM
 * 	PTE_ADDR() will return the first 20 bits in pte
 *
 * PTE_FLAGS(pte)
 * 	this returns the flag bits of page table entry pte
 * 	by clearing the PPN bits
 */
#define PTE_ADDR(pte)   ((uint)(pte) & ~0xFFF)
#define PTE_FLAGS(pte)  ((uint)(pte) &  0xFFF)

#ifndef __ASSEMBLER__
typedef uint pte_t;

// Task state segment format

/* Arjun
 * Task State Segment (TSS) contains all the information needed
 * by the processor in order to manage tasks
 *
 * Q: What is the use of TSS?
 *
 * It basically contains
 *
 * 1. dynamic attributes
 * 	general registers
 * 	segment registers
 * 	flags register
 * 	instrution pointer
 * 	selector of TSS of previously executing task
 *
 * 2. static attributes
 *	selector of the task's LDT
 *	page directory base register of that task
 *	pointers to the stack for PL 0, 1, 2
 *	debug trap bit
 *	I/O map base
 *
 * The task register in the CPU identifies the currently
 * executing task
 */
struct taskstate {
  uint link;         // Old ts selector
  uint esp0;         // Stack pointers and segment selectors
  ushort ss0;        // after an increase in privilege level
  ushort padding1;
  uint *esp1;
  ushort ss1;
  ushort padding2;
  uint *esp2;
  ushort ss2;
  ushort padding3;
  void *cr3;         // Page directory base
  uint *eip;         // Saved state from last task switch
  uint eflags;
  uint eax;          // More saved state (registers)
  uint ecx;
  uint edx;
  uint ebx;
  uint *esp;
  uint *ebp;
  uint esi;
  uint edi;
  ushort es;         // Even more saved state (segment selectors)
  ushort padding4;
  ushort cs;
  ushort padding5;
  ushort ss;
  ushort padding6;
  ushort ds;
  ushort padding7;
  ushort fs;
  ushort padding8;
  ushort gs;
  ushort padding9;
  ushort ldt;
  ushort padding10;
  ushort t;          // Trap on task switch
  ushort iomb;       // I/O map base address
};

// Gate descriptors for interrupts and traps

/* Arjun
 *
 * gatdesc structure defines a gate descriptor, this one
 * is in particular for interrupts and traps in x86
 *
 * interrupt / trap gates directly point to the procedure
 * which will execute in the context of the currently executing
 * task
 *
 * the structure is a bitmap, and both have the same bitmap
 * except for the type bits, (4 bits) that are different
 *
 * off_15_0
 * 	lower 16 bits of offset
 * 
 * cs
 * 	code segment selector, cs of interrupt service routine
 *
 * type
 * 	STS_IG32 (0xE) for interrupt gate
 * 	STS_TG32 (0xF) for trap gate	
 * dpl
 * 	privilege level of the descriptor, which is checked
 * 	with the CPL (current privilege level) before jumping
 * 	to the interrupt handler / service routine
 *
 */
struct gatedesc {
  uint off_15_0 : 16;   // low 16 bits of offset in segment
  uint cs : 16;         // code segment selector
  uint args : 5;        // # args, 0 for interrupt/trap gates
  uint rsv1 : 3;        // reserved(should be zero I guess)
  uint type : 4;        // type(STS_{IG32,TG32})
  uint s : 1;           // must be 0 (system)
  uint dpl : 2;         // descriptor(meaning new) privilege level
  uint p : 1;           // Present
  uint off_31_16 : 16;  // high bits of offset in segment
};

// Set up a normal interrupt/trap gate descriptor.
// - istrap: 1 for a trap (= exception) gate, 0 for an interrupt gate.
//   interrupt gate clears FL_IF, trap gate leaves FL_IF alone
// - sel: Code segment selector for interrupt/trap handler
// - off: Offset in code segment for interrupt/trap handler
// - dpl: Descriptor Privilege Level -
//        the privilege level required for software to invoke
//        this interrupt/trap gate explicitly using an int instruction.


/* Arjun
 *
 * sets the various attributes of gatedesc data structure, based on the
 * parameters given to SETGATE macro
 *
 * off_15_0 	stores lower 16 bits of offset, obtained by masking
 * 	    	off with 0xffff
 *
 * cs		is 16 bit, set to the parameter given directly
 *
 * args 	set to 0, meaning all gates are intr / trap gates
 *
 * rsv1		set to 0, reserved
 *
 * type		set to STS_TG32 (0xf) for trap (sowftware interrupt)
 * 		       when istrap == 1
 * 		       STS_IG32 (0xe) for hardware interrupt 
 * 		       when istrap == 0
 *
 * s		set to 0
 *
 * dpl		new descriptor privilege level,
 * 		set it to the parameter directly
 *
 * p		present set to 1
 *
 * off_31_16	higher 16 bits of offset, obtained by left shifting
 * 		off 16 times
 *
 * SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
 */
#define SETGATE(gate, istrap, sel, off, d)                \
{                                                         \
  (gate).off_15_0 = (uint)(off) & 0xffff;                \
  (gate).cs = (sel);                                      \
  (gate).args = 0;                                        \
  (gate).rsv1 = 0;                                        \
  (gate).type = (istrap) ? STS_TG32 : STS_IG32;           \
  (gate).s = 0;                                           \
  (gate).dpl = (d);                                       \
  (gate).p = 1;                                           \
  (gate).off_31_16 = (uint)(off) >> 16;                  \
}

#endif
