//
// assembler macros to create x86 segments
//

/* Arjun
 *
 * SEG_NULLASM basically defines a series of words
 * and bytes, which are zero
 */
#define SEG_NULLASM                                             \
        .word 0, 0;                                             \
        .byte 0, 0, 0, 0

// The 0xC0 means the limit is in 4096-byte units
// and (for executable segments) 32-bit mode.


/* Arjun
 *
 * really not sure what this means, as of now
 * 
 * Pratham
 *
 * The following macro is to setup the segment table in the memory.
 * The following is called to setup the CODE segment and the
 * DATA segment. (type, base, limit) 
 * The above (SEG_NULLASM) macro is used to setup an initial null segment.
 *
 * /

#define SEG_ASM(type,base,lim)                                  \
        .word (((lim) >> 12) & 0xffff), ((base) & 0xffff);      \
        .byte (((base) >> 16) & 0xff), (0x90 | (type)),         \
                (0xC0 | (((lim) >> 28) & 0xf)), (((base) >> 24) & 0xff)

#define STA_X     0x8       // Executable segment
#define STA_W     0x2       // Writeable (non-executable segments)
#define STA_R     0x2       // Readable (executable segments)
