/* sleeplocks are used for tasks that require a lock to be held for 
 * a long time
 *
 * if a process tries to aqcuiresleep() a resource protected by a 
 * sleeplock, and if some other process is using the resource, then
 * the process trying will be put to sleep
 *
 * it is the responsibility of the process that is holding the sleeplock
 * to wakeup() any processes that are sleeping for that resource
 *
 * sleeplock itself needs to be protected by a spinlock, since that is the rule!
 * "use the lower level synchronization primitive in order to build higher
 * level mechanisms"
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

