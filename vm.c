#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

/* Arjun
 * Q: How is the value of data set, and by whom?
 * Q: Is seginit() used to "turn off", rather bypass 
 * segmentation in x86?
 */
extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.

/* Arjun
 * seginit() is used for setting up the segmentation table for xv6
 * this is "bypassed", since only paging is used in xv6
 */
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  
  /* Arjun
   * this code below created 4 segment entries into the gdt
   * these segment entries are anyways never used
   * 
   * hardcoded attributes make it a
   * 1. system segment (only sys can use it)
   * 2. present bit is set
   * 3. not available for other usage
   * 4. not reserved
   * 5. it is a 32 bit segment
   * 6. it is granular
   *
   * segments created are indicated as:	
   * the limit is interpreted as a multiple of 4096 units,
   * hence we only need 20 bits to set the limit as 4GB
   * since lg(2^32 / 2^12) = lg(2^20) = 20
   * 
   * SEG_KCODE
   * 	used for kernel code
   * 	type	STA_X | STA_R, i.e. readable and executable
   * 	0	base address
   * 	0xf...	limit
   * 	0	dpl is set to kernel
   *
   * SEG_KDATA
   * 	used for kernel data
   * 	type	STA_W, i.e. writeable
   * 	0	base address
   * 	0xf...	limit
   * 	0	dpl is set to kernel
   *
   * SEG_UCODE
   * 	used for user code
   * 	type	STA_X | STA_R, i.e. readable and executable
   * 	0	base address
   * 	0xf...	limit
   * 	0	dpl is set to DPL_USER, i.e. 3
   *
   * SEG_UDATA
   * 	used for user data
   * 	type	STA_W, i.e. writeable
   * 	0	base address
   * 	0xf...	limit
   * 	0	dpl is set to DPL_USER, i.e. 3
   */
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);

  /* call lgdt() to load addres of gdt into CPU regsiter
   * this will make the CPU's LGDT base register point to the
   * gdt global variabl, i.e. the gdt that we just "initialised"
   *
   * from now on, this gdt will be used for segmentation, and it
   * is just there for formality, it is bypassed
   */
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.

/* Arjun
 *
 * walkpgdir() returns the address of the PTE in pgdir
 * that corresponds to virtual address va
 *
 * if no such PTE is found, and alloc != 0
 * then a page is created
 */
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  
  pde_t *pde;
  pte_t *pgtab;
  
  /* Arjun
   * pde points to the page directory entry in pgdir
   * PDX(va) returns an index into the page directory
   * by basically returning the uppermost 10 bits in 
   * va, which contain the page directory index
   */
  pde = &pgdir[PDX(va)];

  /* Now, pde is masked with PTE_P (1)
   * to check whether the entry is valid or not
   *
   * if *pde is valid
   *	we need to proceed to get the address of 
   *	the page table which is inside *pde
   *
   *	DOUBTFUL {
   *	PTE_ADDR(*pde) wil return the physical page number, 
   *	PPN from the page table entry *pde
   *	}
   *
   *	P2V will add KERNBASE for converting physical address
   *	into a virtual address, and once it is casted to 
   *	(pte_t *), it can be returned as the address of the page table
   *
   * if *pde is invalid
   * 	a page table needs to be created in order to make it valid
   * 	if alloc != 0 and kalloc() successfuly returns a pointer
   * 	to the newly allocated page for the page table, then we can
   * 	proceed to set the flags
   *
   * 	memset() is called for the page table to set PTE_P bits
   * 	to zero
   * 	then, certain permissions are set
   */
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }

  /* finally,
   * we return the address of the PTX(va) th page table
   * entry, by using PTX(va) as an index into pgtab
   */
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  /* Arjun
   *
   * va is grounded down, the value is stored in a
   *
   * va + size - 1 is grounded down, the value is stored in 
   * last
   *
   * Q: Why do we need to add "size - 1" to va,
   * why not just "size"
   * A: This is because the nature of the loop below is like a 
   * "do while" loop. After the page table entry is made, then the
   * value of a is checked with last
   *
   * For example,
   * 	va  	4096
   * 	size	8192
   *
   * 	This means that one wants to map 8192 (8K) bytes after virtual
   * 	address 4096 to some physical address pa
   *
   *	Hence, 2 pages must be mapped
   *	The loop below must run twice for this to happen
   *
   *	If we do not subtract 1 from va + size before grounding
   *	the address down to a page boundary, then this is happens:
   *	
   *	initially,
   * 	a	= PGROUNDDOWN((uint) va) = PGROUNDDOWN(4096) = 4096
   * 	last	= PGROUNDDOWN(((uint) va) + size) = PGROUNDDOWN(4096  + 8192)
   * 		= 12288
   *
   *	iter	a	last	a == last
   *	0	4096	12288	0
   *	1	8192	12288	0
   *	2	12288	12288	1
   *
   *	This is not desired, since 3 iterations are performed
   *
   * 	However, subtracting 1 will now make the following happen
   *	
   *	initially,
   * 	a	= PGROUNDDOWN((uint) va) = PGROUNDDOWN(4096) = 4096
   * 	last	= PGROUNDDOWN(((uint) va) + size - 1) = PGROUNDDOWN(4096 + 8192 - 1)
   * 		= PGROUNDDOWN(12288 - 1)
   * 		= PGROUNDDOWN(12287) = 8192
   *
   *	iter	a	last	a == last
   *	0	4096	8192	0
   *	1	8192	8192	1
   *
   * 	Hence, only 2 pages are mapped, which is what we desired to do
   *
   */
  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);

  /* a will increment by PGSIZE on each iteration,
   * until is equal to the value of last
   *
   * for each a
   * 	call walkpgdir() for getting the address of the
   * 	page table for virtual address a (it may not exist)
   *
   * 	if it does not exist, walkpgdir() is instructed
   * 	to kalloc() a new page table and return the new page
   * 	table's address, which will be stored in pte
   *
   * 	if kalloc() also fails, return from the mappages()
   *
   * 	now, check if PTE_P is set
   *
   * 	set necessary permissions and also make the PPN in
   * 	*pte (the 0th entry in the page table) contain the value
   * 	pa
   *
   * 	increment a and pa both by PGSIZE
   */
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.


/* Arjun
 *
 * a single struct kmap is a kernel mapping, of virtual address to
 * the range of physical addresses, along with some permissions
 *
 * the kmap[] array (the name is same as the struct name)
 * contains 4 entries, to map the virtual addresses to the corresponding
 * physical memory regions
 *
 * Name(Value)			From		To			Perms	
 * KERNBASE(2 GB)		0		1MB			writeable
 * 
 * KERNLINK(KERNBASE + 1MB)	1MB		1MB + 32K		readonly
 *
 * data(set as 0x80108000)	1MB + 32K	224MB			writeable
 *
 * DEVSPACE(other devices)	4064MB		0			writeable
 *
 */
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  /* allocate one page for the pgdir using kalloc()
   * and cast the char * returned by kalloc() as a
   * pde_t * 
   *
   * check if kalloc() succeeded
   */
  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;

  /* call memset() for the page obtained
   */
  memset(pgdir, 0, PGSIZE);

  /* some error handling
   */
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");

  /* use k as a pointer to traverse the kmap array
   * until it reaches the end of the array
   *
   * NELEM(kmap) is replaces by the number of elements in kmap
   * (which is 4)
   * &kmap[4] is the address just after the address of the last
   * element in kmap[]
   *
   * k++ will increment k by sizeof(struct kmap)
   *
   * for each entry in kmap[]
   * 	call mappages()
   *
   * 	mappages() will map the virtual memory starting at
   * 	k->virt, for (k->phys_end - k->phys_start) bytes ahead
   * 	to the physical memory region starting at k->phys_start,
   * 	with permission specified in k->perm
   *
   * If any call to mappages() fails, then free the virtual memory
   * allocated to pgdir by calling freevm() on pgdir
   *
   * the calls to mappages() will be as follows
   *
   * mappages(pgdir, KERNBASE, EXTMEM - 0, 0, PTE_W)
   * i.e.	mappages(pgdir, 2GB, 1MB, 0, PTE_W)
   *
   * mappages(pgdir, KERNLINK, V2P(data) - V2P(KERNLINK), V2P(KERNLINK), 0)
   * i.e.	mappages(pgdir, 2GB + 1MB, V2P(0x80108000) - V2P(0x8010000), 0)
   * i.e.	mappages(pgdir, 2049MB, 0x8000, 0)
   * i.e.	mappages(pgdir, 2049MB, 32KB, 0)
   *
   * mappages(pgdir, data, PHYSTOP - V2P(data), PHYSTOP, PTE_W)
   * i.e.	mappages(pgdir, 2GB + 1MB + 32KB, 224MB - 1MB - 32KB, PTE_W)
   *
   * mappages(pgdir, DEVSPACE, 0 - DEVSPACE, DEVSPACE, PTE_W)
   * i.e	mappages(pgdir, 4064MB, 0 - 4064MB, 4064MB, PTE_W)
   * i.e	mappages(pgdir, 4064MB, -32MB, 4064MB, PTE_W)
   */
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.

/* Arjun
 * CR3 register contains the PDB address (page directory base address)
 * of the currently running process
 *
 * loading this by the physical address of kpgdir basically means that
 * the mmu will use kpgdir as its page directory
 */
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{

  /* Arjun
   * some error handling
   *
   * if(p == 0):
   * 	ensures that p is not NULL
   *
   * if(p->kstack == 0):
   * 	ensures that the process has a kernel stack
   *
   * if(p->pgdir == 0):
   * 	ensures that the process has a pgdir pointer
   */
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  /* this is just used to clear interrupts
   */
  pushcli();
 
  /* set the values in the TSS segment of gdt in CPU
   * to those of the current process, to which control is
   * to be transfered
   *
   * type	STS_T32A, for 32 bit TSS
   * base	base is the base address of mycpu()->ts
   * limit	limit is the sizeof(mycpu()->ts) - 1
   * dpl	why is dpl 0?
   *
   * then, the s field is set to 0, meaning that it
   * will be used by the system
   *
   * ss0	is made to point to the process' kernel stack
   *
   * eps0	this is made to point to the top of the stack
   * 		it is calculated by p->kstack - KSTACKSIZE 
   */
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  /* Now,
   * ltr will load the task register with the value SEG_TSS << 3
   * lcr3 will make the processor's cr3 register (PDBR) point to
   * the process' address space
   * in the end, enable interrupts
   */
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

