#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "mmap.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"

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
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
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

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
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
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
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

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  // Clear all mapping metadata
  np->currentMappingIndex = 0;
  for(int i = 0; i < 100; i++) {
    np->mapEntries[i].flags = 0;
    np->mapEntries[i].isValid = -1;
    np->mapEntries[i].numPages = 0;
    np->mapEntries[i].fd = 0;
    np->mapEntries[i].startingVirtualAddress = (void*)0;
    np->mapEntries[i].guardPageVirtualAddress = (void*)0;
  }
  // Copy mapping metadata from parent process
  int res = copymemorymappings(np);

  release(&ptable.lock);

  // Check if error in copying mapping metadata from parent to child process
  if(res == -1) {
    cprintf("Error with copying mapping metadata from parent to child process\n");
    // panic("fork");
  }

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  // cprintf("Inside Exit call\n");

  // Clear all mapping metadata
  for(void* va = (void*)0x60000000; va < (void*)KERNBASE; va+=PGSIZE) {
    pte_t *pte = walkpgdirwrapper(curproc->pgdir, va, 0);
    // Check if page table entry is valid for given virtual address
    if(pte == 0 || (*pte & PTE_P) == 0) {
      continue;
    }
    // Clear the page table entry
    int res = clearpte(curproc->pgdir, va);
    if(res == -1) {
      cprintf("Error with clearing page table entries of process upon exit()\n");
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

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
      havekids = 1;
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
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

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
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
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
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
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
  // Go to sleep.
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

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
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

// Helper function that checks if a given physical address is mapped to some virtual address
int ispamapped(void* pa) {
  struct proc *p;
  // Iterate over all processes in the process table
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    // Check if process is in use
    if(p->state == UNUSED) {
      continue;
    }
    pde_t *pgdir = p->pgdir;
    // Iterate over all virtual addresses in range
    for(void* va = (void*)0x60000000; va < (void*)KERNBASE; va+=PGSIZE) {
      pte_t *pte = walkpgdirwrapper(pgdir, va, 0);
      // Check if page table entry is valid
      if(pte == 0 || (*pte & PTE_P) == 0) {
        continue;
      }
      void* physicalAddress = P2V(PTE_ADDR(*pte));
      if(physicalAddress == pa) {
        return 0;
      }
    }
  }
  return -1;
}

// Helper function to find mapping metadata in process structure
int findmappingmetadata(void* va) {
  for(int i=0;i<100;i++) {
    struct mappingEntry m = myproc()->mapEntries[i];
    if(m.isValid == 0 && m.startingVirtualAddress == va) {
      return i;
    }
  }
  return -1;
}

// Helper function to check if a given mapping can grow upwards
int cangrowup(void* va, struct proc* currentProcess) {
  // cprintf("Address accessed: %p\n", va);
  int numMappings = currentProcess->currentMappingIndex + 1;
  // cprintf("Number of Mappings: %d\n", numMappings);
  for(int i = 0; i < numMappings; i++) {
    struct mappingEntry currentMap = currentProcess->mapEntries[i];
    // Check if mapping is valid
    if(currentMap.isValid == -1 || currentMap.startingVirtualAddress < (void*)0x60000000 || currentMap.startingVirtualAddress >= (void*)KERNBASE) {
      continue;
    }
    // Store virtual address of start of mapping
    void* startingVirtualAddress = currentMap.startingVirtualAddress;
    // cprintf("Starting Virtual Address: %p\n", startingVirtualAddress);
    int numPages = currentMap.numPages;
    // cprintf("Number of Pages: %d\n", numPages);
    // Store virtual address of start of guard page
    void* endingVirtualAddress = startingVirtualAddress + (numPages * PGSIZE);
    // cprintf("Ending Virtual Address: %p\n", endingVirtualAddress);
    // Store virtual address of page after guard page
    void* nextPageVirtualAddress = startingVirtualAddress + ((numPages+1) * PGSIZE);
    // cprintf("Next Page Virtual Address: %p\n", nextPageVirtualAddress);
    int flags = currentMap.flags;
    int isInRange = -1;
    int isNextPageFree = -1;
    // Check if given address is within the guard page
    if(endingVirtualAddress <= va && nextPageVirtualAddress > va) {
      // cprintf("Is In Range\n");
      isInRange = 0;
    }
    // Check if page after guard page is not mapped
    if(canallocatva(currentProcess->pgdir, nextPageVirtualAddress, 1) == 0) {
      // cprintf("Next Page is Free\n");
      isNextPageFree = 0;
    }
    // Check if grows up flag is set for current mapping
    if(flags & MAP_GROWSUP && isInRange == 0 && isNextPageFree == 0) {
      // cprintf("Possible to grow up\n");
      // Allocate new page in physical memory
      char* physicalPage = kalloc();
      // cprintf("Physical Page Address %p\n", physicalPage);
      // Map current guard page to newly allocated page in physical memory
      int mapResult = mappageswrapper(currentProcess->pgdir, endingVirtualAddress, PGSIZE, V2P(physicalPage), PTE_W|PTE_U);
      if(mapResult != 0) {
        cprintf("Failed to map physical address to virtual address\n");
        return -1;
      }
      // cprintf("Mapped Page %d at %p\n", numPages+1, endingVirtualAddress);
      // Update mapping metadata
      currentProcess->mapEntries[i].numPages++;
      currentProcess->mapEntries[i].guardPageVirtualAddress += PGSIZE;
      return 0;
    }
  }
  return -1;
}

// Helper function to copy mapping metadata from parent to child process
int copymemorymappings(struct proc* child) {
  // cprintf("Current Process: %d\n", myproc()->pid);
  // cprintf("Parent Process: %d\n", child->parent->pid);
  // cprintf("Child Process: %d\n", child->pid);
  
  // Store parent and child process page directories
  struct proc* parent = child->parent;
  int numMappings = parent->currentMappingIndex + 1;
  // cprintf("Number of Mappings: %d\n", numMappings);
  pde_t* parentPgDir = parent->pgdir;
  pde_t* childPgDir = child->pgdir;
  // Copy mappings from parent to child process
  for(int i=0;i<numMappings;i++) {
    struct mappingEntry currentMap = parent->mapEntries[i];
    // Check if mapping is valid
    if(currentMap.isValid == -1 || currentMap.startingVirtualAddress < (void*)0x60000000 || currentMap.startingVirtualAddress >= (void*)KERNBASE) {
      continue;
    }
    void* startingVirtualAddress = currentMap.startingVirtualAddress;
    // cprintf("Starting Virtual Address: %p\n", startingVirtualAddress);
    int numPages = currentMap.numPages;
    // cprintf("Number of Pages: %d\n", numPages);
    int flags = currentMap.flags;
    int fd = currentMap.fd;
    void* guardPageVirtualAddress = currentMap.guardPageVirtualAddress;
    child->mapEntries[child->currentMappingIndex].fd = fd;
    child->mapEntries[child->currentMappingIndex].flags = flags;
    child->mapEntries[child->currentMappingIndex].isValid = 0;
    child->mapEntries[child->currentMappingIndex].numPages = numPages;
    child->mapEntries[child->currentMappingIndex].startingVirtualAddress = startingVirtualAddress;
    child->mapEntries[child->currentMappingIndex].guardPageVirtualAddress = guardPageVirtualAddress;
    child->currentMappingIndex++;
    // In the case of private mapping
    if(flags & MAP_PRIVATE) {
      char *physicalPages[numPages];
      // Allocate new pages in physical memory
      for(int i = 0; i < numPages; i++) {
        physicalPages[i] = kalloc();
        // cprintf("Physical Page Address %d: %p\n", i, physicalPages[i]);
        if (physicalPages[i] == 0) {
          for (int j = 0; j < i; j++) {
            kfree(physicalPages[j]);
          }
          cprintf("Failed to allocate %d pages in physical memory\n", numPages);
          return -1;
        }
      }

      // Find a free virtual address in parent process to temporarily store mapping to newly allocated pages in physical memory
      void* tempVirtualAddress = findfreevirtualaddress(parentPgDir, numPages);
      if(tempVirtualAddress == (void*)-1) {
        cprintf("Unable to find a free virtual address in parent process\n");
        return -1;
      }

      // Map pages in physical memory allocated earlier to the virtual addresses starting from startingVirtualAddress
      for(int i = 0; i < numPages; i++) {
        int mapResultChild = mappageswrapper(childPgDir, startingVirtualAddress, PGSIZE,V2P(physicalPages[i]), PTE_U | PTE_W);
        if(mapResultChild != 0) {
          cprintf("Failed to map physical address to virtual address in child process\n");
          return -1;
        }
        // cprintf("Mapped Page %d at %p\n", i+1, startingVirtualAddress);

        int mapResultParent = mappageswrapper(parentPgDir, tempVirtualAddress, PGSIZE,V2P(physicalPages[i]), PTE_U | PTE_W);
        if(mapResultParent != 0) {
          cprintf("Failed to map physical address to virtual address in parent process\n");
          return -1;
        }
        // cprintf("Mapped Page %d of Parent at %p\n", i+1, tempVirtualAddress);
        // Copy data from parent virtual address to corresponding child virtual address
        char buffer[PGSIZE+1];
        memmove(buffer, startingVirtualAddress, PGSIZE);
        memmove(tempVirtualAddress, buffer, PGSIZE);
        // cprintf("After memmove\n");
        // Clear temporarily stored mapping so that it can be reused in next iteration
        pte_t * tempPte = walkpgdirwrapper(parentPgDir, tempVirtualAddress, 0);
        *tempPte = 0;
        startingVirtualAddress += PGSIZE;
        tempVirtualAddress += PGSIZE;
      }
    }
    // In the case of shared mapping
    else if(flags & MAP_SHARED) {
      for(int i = 0;i < numPages; i++) {
        // Find physical page to which a virtual address is mapped in parent process
        pte_t *parentPte = walkpgdirwrapper(parentPgDir, startingVirtualAddress, 0);
        void* physicalPage = P2V(PTE_ADDR(*parentPte));
        // Create mapping between corresponding virtual address and same physical page in child process
        int mapResult = mappageswrapper(childPgDir, startingVirtualAddress, PGSIZE,V2P(physicalPage), PTE_U | PTE_W);
        if(mapResult != 0) {
          cprintf("Failed to map physical address to virtual address\n");
          return -1;
        }
        // cprintf("Mapped Page %d at %p\n", i+1, startingVirtualAddress);
        startingVirtualAddress += PGSIZE;
      }
    }  
  }
  return 0;
}

// Helper function to check if a given virtual address is a guard page for some mapping
int isguardpage(void* va) {
  int numMappings = myproc()->currentMappingIndex + 1;
  for(int i=0;i<numMappings;i++) {
    struct mappingEntry currentMap = myproc()->mapEntries[i];
    // Check if mapping is valid
    if(currentMap.isValid == -1 || currentMap.startingVirtualAddress < (void*)0x60000000 || currentMap.startingVirtualAddress >= (void*)KERNBASE) {
      continue;
    }
    void* guardPageVirtualAddress = currentMap.guardPageVirtualAddress;
    if(guardPageVirtualAddress == va) {
      return 0;
    }
  }
  return -1;
}