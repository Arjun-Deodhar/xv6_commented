/* sleeplocks are used for tasks that require a lock to be held for 
 * a long time
 *
 * this improves performance, since sloe tasks like disk access would
 * make the process hold the spinlock for a very long time, causing others
 * to spin
 *
 * sleeplock itlsef needs to be protected by a spinlocks, since that is the rule
 * use the lower level synchronization primitive in order to build higher
 * level mechanisms
 *
 * not using the spinlock can cause a race to access the sleeplock!
 */
// Long-term locks for processes
struct sleeplock {
  uint locked;       // Is the lock held?
  struct spinlock lk; // spinlock protecting this sleep lock
  
  // For debugging:
  char *name;        // Name of lock.
  int pid;           // Process holding lock
};

