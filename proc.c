#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure

/* Arjun
 * This function basically returns a pointer to (struct proc)
 * the current process that is running on the cpu
 *
 * mycpu() is called since the architecture is multiprocessor
 * pushcli(), popcli() just ensure that mycpu() is not called
 * with interrupts enabled
 */
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.

/* Arjun
 * allocates a PCB, rather marks a PCB as under use
 * from the global ptable data structure
 */
static struct proc*
allocproc(void)
{
  /* p will traverse the array */
  struct proc *p;
  char *sp;

  /* does some locking to prevent deadlocks ? */
  acquire(&ptable.lock);

  /* find the first unused proc data structure in the array of
   * processes in the global ptable 
   *
   * if found
   * 	jump to "found"
   * if not found, then process limit has been reached (NPROC)
   * 	return 0 in this case
   * 
   * p->state is used for checking a proc for unused or not
   */
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  /* relase lock if not found */
  release(&ptable.lock);
  return 0;

found:
  /* mark the found process's state as EMBRYO
   * allot a pid based on the global variable nextpid
   * increment nextpid
   */
  p->state = EMBRYO;
  p->pid = nextpid++;

  /* release the lock */
  release(&ptable.lock);

  // Allocate kernel stack.

  /* allocates some stack for p, by kalloc()
   * if kalloc() fails, i.e. returns 0
   * then the p in the ptable's proc array is marked
   * as UNUSED, and function returns
   */
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }

  /* make sp point to KSTACKSIZE chars after
   * p->kstack, where KSTACKSIZE is a global
   * with value 4096
   */
  sp = p->kstack + KSTACKSIZE;

  /* what is trap frame?
   *
   * (maybe)
   * it is a collection of all the registers, gen + seg etc
   * and it might be used to save the context before switching 
   * to another process, i.e. calling the scheduler
   * trapframe data structure also includes padding fields
   */
  // Leave room for trap frame.

  /* sp is being made to point sizeof *p->tf bytes (chars)
   * below where it is pointing to right now
   */
  sp -= sizeof *p->tf;

  /* tf is casted into a trapframe pointer and it
   * points to where sp points, this basically means that
   *
   * KSTACKSIZE allocated, sp points to
   * 	p->kstack + KSTACKSIZE - sizeof *pt->tf
   * 	p->tf points to where sp points, but it is casted
   * 	into a trapframe *, so it will return sizeof(trapframe)
   * 	bytes on being dereferenced :)
   *
   * 	this is like giving separate meanings to two parts of the
   * 	malloced (rather, kalloced memory :))
   */
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  /* sp points 4 bytes below where it used to point */
  sp -= 4;
  /* some "return value from trap" is stored in place of sp
   * sp, although being a char *, is cast into a uint * before dereferencing,
   * which will make it return 4 bytes, rather sizeof(uint) bytes when
   * dereferenced
   *
   * this is so that when forkret() returns, control will jump to trapret()
   */
  *(uint*)sp = (uint)trapret;

  /* sp points sizeof(context) below itself 
   *
   * context data structure is just a collection of
   * 	edi, esi, ebx, ebp, eip
   * 	registers
   *
   * these are sufficient to save the context of a process, before switching
   */
  sp -= sizeof *p->context;
  /* similar pointer manipulation just like p->tf earlier
   * giving separate meanings to two parts of the kalloced memory
   */
  p->context = (struct context*)sp;
  /* memset() inserts some assembly language instruction to copy a sequence of
   * bytes from one location to another
   * 
   * p->context->eip is set to the address of forkret(), which
   * is where the new process will begin execution, when it is
   * scheduled by the scheduler for the first time
   */
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  /* Arjun
   * allocate a process by calling allocproc()
   * this will have pid 1, since nextpid is set to 1
   * the next process after this will thus have pid 2
   *
   * copy the code of init into a page allocated by calling
   * kalloc(), and enter its mapping into p->pgdir
   *
   * address of the code is given by _binary_initcode_start
   * size of the code is given by _binary_initcode_size
   */
  p = allocproc();
  
  /* set global initproc to p, since this will be refered to in exit()
   */
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);

  /* set the follwing
   * size of the process - 4KB (1 page size)
   *
   * call memset() for the trapframe, insert 0s into it
   * p->tf->cs to the user's code segment
   * p->tf->ds to the user's data segment
   * copy the ds value into es, ss
   *
   * set flags to FL_IF, which contains enabled interrupts
   * set esp to PGSIZE, i.e. top of the stack
   * eip is 0, which is the beginning of initcode.S
   *
   * this code is over here due to inituvm() (maybe)
   *
   * set name of process as "initcode"
   * current working directory is the inode of "/"
   */
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  /* switchuvm() is done so that the TLB entries are invalidated, 
   * since cr3 is reloaded :)
   *
   * WOW!
   */
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;

  /* curproc gets a pointer to the currently running
   * process on the CPU
   *
   * myproc() calls mycpu() and returns mycpu()->curproc
   */
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  /* call copyuvm() for copying the curproc's pgdir into np's pgdir
   * this involves allocating the page tables required as well
   *
   * if the call fails, then np->kstack is kfree()d and fork() fails
   */
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  /* for all the file pointers in ofile[]
   * 	call filedup() to copy the open pointer into the
   * 	np->ofile[]
   *
   * call idup() to copy the inode of curproc->cwd to np->cwd
   */
  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  /* copy the curproc's name into np->name
   * set pid (to be returned to parent) as np->pid
   * make np RUNNABLE, acquire() lock on ptable for this
   */
  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.

/* exit the current process, by setting the state to ZOMBIE
 * and calling sched()
 *
 * wait() will clean up this process, either called by init
 * or by this process' parent
 *
 * init should not call exit
 */
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  // call fileclose() for each open file
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  // put the curproc->cwd inode back if this is the last process
  // that references it
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  // wakeup1() the parent that might have called wait()
  wakeup1(curproc->parent);

  /* if there are any children of this process, then make init their parent
   * this ensures that no orphans exist, since all of them will be attached
   * to init as a parent
   *
   * if the state of the child is ZOMBIE, which means that curproc
   * did not call wait() on it, then wakeup1() initproc, since that might
   * be waiting here, because wait() called by initproc might be sleeping on
   * this channel
   *
   * Q: Why is this wakeup1() call necessary?
   *
   * This wakeup1() is necessary, because it is possible that the following sequence
   * of events may occus:
   *
   * initially: 
   * curproc (the process that has called exit() is RUNNING)
   *
   * init is RUNNABLE, it was interrupted when it had called wait(), and it was 
   * checking for child processes
   *
   * a child of curproc is RUNNABLE, just which is just about to exit()
   *
   * 1. curproc: sets p->parent to initproc
   *
   * 2. timer interrupt, scheduler schedules init
   *
   * 3. init sees that there is a child, sets havekids to 1
   * 	however, child's state is not ZOMBIE, and hence 
   *
   * 	init is about to call sleep() on the channel of the child's struct proc
   *
   * 4. timer interrupt, scheduler schedules the child
   *
   * 5. child calls exit(), which calles wakeup() on child's parent
   * 	however, init is not waiting for it yet (since init is RUNNABLE, not SLEEPING)
   * 	hence this wakeup is lost
   *
   *	 exit() completes and child becomes a ZOMBIE, calls sched() to give up CPU
   *
   * 6. scheduler runs, schedules init
   *
   * 7. init now calls sleep() on the child, calls sched() to give up CPU
   *
   * NOW in this scenario, had curproc not called wakeup1(), init would
   * have NEVER been woken up by anyone!
   *
   * Hence, the call to wakeup1() is necessary
   *
   * Q: this means that ZOMBIE processes will be cleared only after the parent
   * calls exit(), even though the parent never intends to call wait() on 
   * the child?
   * A: Yes
   * 
   */
  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
	  // set havekids to 1, meanig that this process has a child/kid
      havekids = 1;
	  // if the child has become a zombie, then free its memory, stack
	  // and set many entries in the struct proc to NULL (0)
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
		// release ptable.lock and return
        release(&ptable.lock);
        return pid;
      }
    }

	// If the loop exited, this means that there are no children
	// of this process
    // No point waiting if we don't have any children.
	// return
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

	// sleep on curproc, holding ptable.lock
    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
/* Arjun */
void
scheduler(void)
{
  struct proc *p;
  /* get the current cpu
   * set c->proc to NULL, since the 
   * scheduler is running on the cpu
   */
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    /* allows to execute sti instruction of x86
     * just a wrapper function
     *
     * this is when interrupts will be enabled for
     * the FIRST time (maybe) after they were disabled
     * by the bootloader code :)
	 *
	 * Q: Why does this sti() need to be inside the infinite loop?
     */
    sti();

    /* the for() loop is the scheduling algorithm of xv6
     * round robin scheduling is used in xv6, which simply
     * picks the next process in ptable, which is in the
     * RUNNABLE state
     */
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.

      /* This will just make the cpu's proc point to the
       * process to be run, this DOES NOT PASS CONTROL
       *
       * switchuvm() is some memory management code for
       * setting up user's virtual memory, will add comments
       * about this later
       *
       * p->state is changed to running, since control
       * will be passed on to the process by swtch()
       */
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      /* This is the interesting piece of code that will
       * pass control to the process :)
	   *
       * the c->scheduler is passed as old context, 
       * p->context is passed as the new context
       */
      swtch(&(c->scheduler), p->context);

      /* we came here, meaning that we have switched back to kernel's
	   * context
	   *
	   * use kpgdir for paging, i.e. switch to
       * kernel's page directory
       */
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
/* Arjun
 *
 * sched() function "invokes" the scheduler, passes control to it
 * by calling swtch() to perform a context switch
 *
 * as of now, there are only 3 points from which sched() is called
 * These are as follows:
 * 1. yield
 * 2. exit
 * 3. sleep
 *
 * from within these functions, the state of the process will
 * be modified and sched() will be called appropriately
 */
void
sched(void)
{
  int intena;

  /* get a pointer to the current process
   * it isn't running anymore, but the cpu's
   * proc pointer still points to it
   */
  struct proc *p = myproc();

  /* error handling code
   *
   * if(!holding(&ptable.lock))
   * 	ensures that a lock is held on the ptable
   *
   * if(p->state == RUNNING)
   * 	ensures that the process state is not running
   */
  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  /* call swtch() to perform the context switch,
   * from the context of the process (old context)
   * to the context of the scheduler (new context)
   */
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  /* error checks
   *
   * ensure that there is some process running on mycpu()
   * ensure that the pointer lk is not null
   */
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep, and call sched
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  // A loop is necessary since there may be multiple processes
  // waiting on chan
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
	  if(p->state == SLEEPING && p->chan == chan)
		  p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
	void
wakeup(void *chan)
{
	// acquire() and release() guards wakeup1()
	acquire(&ptable.lock);
	wakeup1(chan);
	release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
