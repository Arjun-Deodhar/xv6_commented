// Mutual exclusion lock.
/* Arjun
 * below is a spinlock structure, which contains the following
 *
 *  locked	boolean indicating whether the lock is held or not
 *  		0: free
 *  		1: held
 *
 * 	name	name of the lock, set in initlock()
 * 	cpu		cpu holding the lock
 * 	pcs		call stack which is an array of funciton pointers
 * 			this is printed out in panic()
 */
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
  uint pcs[10];      // The call stack (an array of program counters)
                     // that locked the lock.
};

