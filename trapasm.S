#include "mmu.h"

# vectors.S sends all traps here.
# alltraps is declared as a global, can be accessed outside
# as well
.globl alltraps
alltraps:
  # Build trap frame.

  # Arjun
  # pusha instruction will cause the registers 
  # to be pushed on the stack in the following order:
  # EAX, ECX, EDX, EBX,
  # the initial value of ESP before EAX was pushed
  # EBP, ESI, and EDI.
  # 
  # since DS, ES, FS, and GS are not pushed, we need to push
  # them manually before pusha is done

  pushl %ds
  pushl %es
  pushl %fs
  pushl %gs
  pushal
  
  # Set up data segments.
  
  # Arjun
  # data segment changed to point to the SEG_KDATA
  # in the GDT, since kernel data needs to be accessed
  # when the trap handler runs
  # hence, ds is loaded with SEG_KDATA << 3, i.e. 2 << 3
  movw $(SEG_KDATA<<3), %ax
  movw %ax, %ds
  movw %ax, %es

  # Call trap(tf), where tf=%esp
  # Q: Why is esp pushed over here?
  pushl %esp
  call trap
  addl $4, %esp

  # Return falls through to trapret...
.globl trapret
trapret:
  # on a return, pop all the resgiters that
  # were pushed while buidling the trap frame
  popal
  popl %gs
  popl %fs
  popl %es
  popl %ds
  # 0x8 is added to esp for "popping" the trap number
  # as well as the error code from the stack
  # iret will switch back to user mode
  addl $0x8, %esp  # trapno and errcode
  iret
