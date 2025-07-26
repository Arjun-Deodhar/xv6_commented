#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

/* Arjun
 * initialise the gatedesc idt[] data sturcture,
 * i.e the interrupt descriptor table
 *	
 * calls SETGATE repeatedly to set values of idt[]
 *
 * all that happens is that the in memory array is set up, 
 * interrupt handling as such is not
 */
void
tvinit(void)
{
  int i;

  /* for all 256 entries in idt[],
   * 	selector set to SEG_KCODE << 3, which is 1 << 3, i.e. 8	
   * 	this is to ensure that the selector is 1000
   *	this ensures:
   *		kernel code segment selected from GDT
   *		cpl is 0, table index is 0
   *
   * 	vectors[i] fetches the offset and sets offset to it
   * 	dpl is set to 0, the privilege level, which is kernel
   * 	istrap is set to 0, meaning hardware interrupt
   * 	this means that the gate is an interrupt gate
   *
   * however, for idt[T_SYSCALL], i.e. idt[64],
   * 	the gate_desc is set to a syscall by passing istrap = 1, which
   * 	means that it is a software interrupt
   * 	this means that the gate is a trap gate
   * 	dpl is set to 3, which is user level privilege
   *
   * 	ALL other interrupt desciptors have DPL set to 0, 
   * 	meaning that only the kernel is allowed to "invoke" the
   * 	interrupts
   *
   * 	This is because the user should only be allowed to do
   * 	system calls and not invoke other interrupt handlers as
   * 	the user pleases, since all other handlers are meant for
   * 	hardware interrupts
   *
   * 	since CPL <= DPL is checked before jumping to the interrupt
   * 	handler, the user cannot simply run an "int xx" instruction,
   * 	where xx != T_SYSCALL
   *
   * 	If CPL > DPL, then this means that the current mode of execution
   * 	does not allow the interrupt handler to be called, which leads to
   * 	a "protection fault", i.e. int 13 
   * 	(an exception indicating some privilege related illegal action)
   */
  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  /* this we will see later */
  initlock(&tickslock, "time");
}


/* Arjun
 * this function makes the IDTR in the cpu
 * (IDTR is Interrupt Descriptor Table Register)
 * point to the interrupt descriptor table idt[]
 */
void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
/* Arjun
 * this function calls the appropriate trap handler
 * based on the value trapno in trapframe tf
 *
 * for example, if tf->trapno is T_SYSCALL (64), then
 * syscall(), the handler for system calls is called
 */
void
trap(struct trapframe *tf)
{
  /* check is the trapno indicates a syscall
   *
   * if it does:
   * 	if the process is killed, then exit()
   * 	make the trapframe of myproc() point
   * 	to the trapframe given as an argument to trap()
   * 	call syscall()
   *
   * 	once it returns, recheck if the process was killed
   * 	(for whatever reason)
   */
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  /* the trap was not for a syscall
   * handle all cases 
   */
  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
