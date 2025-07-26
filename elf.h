// Format of an ELF executable file


/* Arjun: FILE COMPLETED
 *
 * this file contains two structs and some macros, related
 * to reading and interpreting ELF files
 *
 * the strutures are elfhdr and proghdr
 * macros are for magic number, and some permissions bits
 */

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// File header
/* Arjun
 * this is the structure that holds the various componenets
 * and information in an elf file header
 * this is used for reading elf files, by functions like
 * bootmain() and exec() (exec should read this, IMO but not so sure)
 *
 * these are basically the attributes as specified in the
 * struct ElfN_Ehdr, described in the man page of elf
 *
 * one difference between this struct and ElfN_Ehdr is that the
 * magic number bits are stored inside the e_ident[16] array in ElfN_Ehdr
 * whereas here, the e_ident[16] array is split into two parts
 * 	elf[12] and magic, thus separating the 4 bytes of magic
 * 	number into a separate variable
 * 	Q: Why did the xv6 programmers do this?
 *
 * magic
 * 	stores the ELF_MAGIC value, which needs to contain a specific
 * 	sequence denoting that the file is in ELF format
 *
 * elf[12]
 * 	array of bytes specifying how to interpret the file,
 * 	independent of the processor or the file's remaining contents
 * 	there are 12 bytes in it, read man page of elf for details
 *
 * type
 *	type of the file, [executable, relocatable, core, ...]
 * 
 * machine
 * 	required architecture for the given executable file
 * 	this should be hardcoded somewhere while compiling the kernel
 * 	therefore it MUST be present in the makefile, as an option to 
 * 	the compiler
 * 	(maybe)
 *
 * version
 * 	version is invalid or current
 *
 * entry
 * 	this is the virtual address of the "entry" point, to which
 * 	control must be passed on to for exeuction
 * 	this entry is cast into a function pointer entry (in bootmain())
 * 	and the entry() function is called
 * 
 * phoff
 * 	offset of the program header table
 *
 * shoff
 *	offset of the section header table	
 *
 * flags
 * 	some processor specific flags
 *
 * ehsize
 * 	size of the elf header
 *
 * phentsize
 * 	size of one entry in the program header table
 * 	
 * phnum
 * 	number of entries in the program header table
 *
 * 	NOTE:
 * 	phnum * phentsize  = size of program header table
 *
 * shentsize, shnum
 * 	similar as phentsize, phnum but for the section header
 * 	table
 *
 * shstrndx
 * 	section header table index of the entry associated with the
 *      section name string table
 *      (not sure what this means)
 *
 * HW: locate where exactly these values are put in when the
 * kernel's elf file is generated
 * who puts in which entry? (are some of them put in by the compiler,
 * some by linker?)
 */
struct elfhdr {
  uint magic;  // must equal ELF_MAGIC
  uchar elf[12];
  ushort type;
  ushort machine;
  uint version;
  uint entry;
  uint phoff;
  uint shoff;
  uint flags;
  ushort ehsize;
  ushort phentsize;
  ushort phnum;
  ushort shentsize;
  ushort shnum;
  ushort shstrndx;
};

/* Arjun
 * this is the structure that holds the various componenets
 * and information of the program header in an elf file
 * again, this is a replica of the Elf32_Phdr struct, described
 * in the man page of elf
 *
 * type
 * 	how to interpret the segment
 * 	types are NULL, LOAD, DYNAMIC, etc...
 * 	read elf man page for details
 *
 * 	Q. Here, for the kernel's elf, this number should
 * 	be set to a loadable segment, right?
 *
 * off
 * 	offset from the beginning of the file at which 
 * 	the first byte of the segment resides
 *
 * vaddr
 * 	virtual address of the first byte at which the first
 * 	byte of the segment resides in memory
 *
 * paddr
 * 	physical address of the segment
 *
 * filesz
 * 	number of bytes in the file image of the segment
 *
 * memsz
 * 	number of bytes in the memory image of the segment
 *
 * flags
 * 	r, w, x permissions associated with the segment
 *
 * align
 * 	value to which the segments are aligned in memory
 * 	and in the file
 *
 * Q: difference between memsz and filesz?
 *
 * filesz is the number of bytes that the segment will occupy
 * ON the file, and memsz is the actual memory to be allocated
 * to the segment
 *
 * 
 */
// Program section header
struct proghdr {
  uint type;
  uint off;
  uint vaddr;
  uint paddr;
  uint filesz;
  uint memsz;
  uint flags;
  uint align;
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
/* permissions for program header segments
 * r, w, x
 */
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
