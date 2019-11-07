#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
// #define FCFS
// #define DEFAULT
// #define PRIORITY
#define MLFQ

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

struct queue{
  // struct spinlock lock;
  struct proc *proc[NPROC];
  int rtime; //slice time
  int wtime;
  int minpri;
  int num;//size of queue, proc[num] is where new process wil go
};

struct queue mlfq[5];

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

  // Initialising the variables
  p->ctime = ticks; 
  p->etime = 0;
  p->rtime = 0;
  p->iotime = 0;
  p->num_run = 0;
  p->priority = 60;
  for(int it=0;it<4;it++) p->ticks[it]=0;

  #ifdef MLFQ

    // In mlfq it starts in queue 0
    acquire(&ptable.lock);
    mlfq[0].proc[mlfq[0].num]=p;
    mlfq[0].num++;  
    p->current_queue = 0;
    release(&ptable.lock);
  #endif

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

  release(&ptable.lock);

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

  // Endtime of process
  curproc->etime = ticks;

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

        #ifdef MLFQ
        //now let's delete from queue
        int jt=-1;
        int it=p->current_queue;
        for(int itt=0;itt<mlfq[it].num;itt++){
          if(mlfq[it].proc[itt]->pid==p->pid){
            jt=itt;
            break;
          }
        }
        if(jt!=-1){
          for(int itt=jt+1;itt<mlfq[it].num;itt++) mlfq[it].proc[itt-1]=mlfq[it].proc[itt];
          mlfq[it].num--;
          mlfq[it].proc[mlfq[it].num]=0;
        }
        else{
          cprintf("This should not be happening\n");
        }
        #endif
        
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

int
waitx(int *wtime,int *rtime)
{
  struct proc *p;
  int havekids, pid;
  struct proc *proc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        *wtime= p->etime - p->ctime - p->rtime - p->iotime;
        *rtime=p->rtime;
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
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
  #ifdef DEFAULT
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
        p->num_run++;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;

      }
      release(&ptable.lock);
  }
  #else
  #ifdef FCFS
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  struct proc *ptorun=0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();
    // cprintf("FCFS!");
      acquire(&ptable.lock);
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE)
          continue;
        
        else if(ptorun==0) 
          ptorun=p;

        else if(p->ctime<ptorun->ctime)
          ptorun=p;
      }

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      if(ptorun!=0){
        c->proc = ptorun;
        switchuvm(ptorun);
        ptorun->state = RUNNING;
        ptorun->num_run++;

        swtch(&(c->scheduler), ptorun->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&ptable.lock);
      ptorun=0; //????
  }
  #else
  #ifdef PRIORITY
  struct cpu *c = mycpu();
  c->proc = 0;
  struct proc *p;
  struct proc *ptorun=0;
  struct proc *p1;

  for(;;){
    // Enable interrupts on this processor.
    sti();
      acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      
      ptorun = p;
      // choose one with highest priority

      for(p1 = ptable.proc; p1 < &ptable.proc[NPROC]; p1++){
        if(p1->state != RUNNABLE)
          continue;
        else if (ptorun->priority>p1->priority )
          ptorun = p1;
      }
      
      if(ptorun!=0){
        c->proc = ptorun;
        switchuvm(ptorun);
        ptorun->state = RUNNING;
        ptorun->num_run++;

        swtch(&(c->scheduler), ptorun->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        ptorun=0; //????
      }
   
    }
      release(&ptable.lock);
  }
  #else
  #ifdef MLFQ
    for(int i=0;i<5;i++){
      mlfq[i].rtime=1;
      for(int j=0;j<i;j++) mlfq[i].rtime*=2;
      mlfq[i].wtime=5;
    }

    struct cpu *c = mycpu();
    c->proc = 0;
    struct proc *ptorun=0;
    int i,j;

    for(;;){
      // Enable interrupts on this processor.
      sti();
      //waittime
      acquire(&ptable.lock);

      //check if any procs have to be moved up 
      for(int it=0;it<5;it++){
        for(int jt=0;jt<NPROC;jt++){
          if(mlfq[it].proc[jt]!=0){
            
            //brute force deleting
            // if(mlfq[it].proc[jt]->pid==0){
            //   cprintf("here %d\n", mlfq[it].proc[jt]->state);
            //   //now time to delete from the queue
            //   for(int itt=jt+1;itt<mlfq[it].num;itt++) mlfq[it].proc[itt-1]=mlfq[it].proc[itt];
            //   mlfq[it].num--;
            //   mlfq[it].proc[mlfq[it].num]=0;
            // }

            if(mlfq[it].proc[jt]->state != RUNNABLE)  
              continue;
            // cprintf("%d intime %d ticks %d\n",mlfq[it].proc[jt]->pid,mlfq[it].proc[jt]->intime,ticks);
            else if(ticks-mlfq[it].proc[jt]->intime>mlfq[it].wtime && it>0){
              //now putting process in the queue newq 
              int newq=it-1;
              mlfq[newq].proc[mlfq[newq].num]=mlfq[it].proc[jt];
              mlfq[newq].proc[mlfq[newq].num]->intime=ticks;
              mlfq[newq].proc[mlfq[newq].num]->ticksnow[newq]=0;
              mlfq[newq].proc[mlfq[newq].num++]->current_queue=newq;

              //now time to delete from the queue
              for(int itt=jt+1;itt<mlfq[it].num;itt++) mlfq[it].proc[itt-1]=mlfq[it].proc[itt];
              mlfq[it].num--;
              mlfq[it].proc[mlfq[it].num]=0;

              ptorun=mlfq[newq].proc[mlfq[newq].num-1];
              cprintf("%d shifted up to %d\n",ptorun->pid,ptorun->current_queue);
            }
          }
        }
      }

      //check which proc to run
      ptorun=0;
      for(i=0;i<5;i++){
        for(j=0;j<mlfq[i].num;j++){ 
          if(mlfq[i].proc[j]!=0){
            //Now checking process in queue i
            if(mlfq[i].proc[j]->state != RUNNABLE)  //change to priority round robin
              continue;
            else {
              ptorun=mlfq[i].proc[j];  //so now ptorun pointing to some address in ptable
              break;
            }
          }
        }
        if(ptorun!=0) break;
      }

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      if(ptorun!=0){ //We just chose process in queue i at position j

        for(int lol=0;lol<5;lol++){
          for(int it=0;it<mlfq[lol].num;it++) {
            if(mlfq[lol].proc[it]->state== RUNNING)
            cprintf("%d RUNNING  ",mlfq[lol].proc[it]->pid,mlfq[lol].proc[it]->state);
            else if(mlfq[lol].proc[it]->state== RUNNABLE)
            cprintf("%d RUNNABLE  ",mlfq[lol].proc[it]->pid,mlfq[lol].proc[it]->state);
            else if(mlfq[lol].proc[it]->state== SLEEPING)
            cprintf("%d SLEEPING  ",mlfq[lol].proc[it]->pid,mlfq[lol].proc[it]->state);
            else if(mlfq[lol].proc[it]->state== UNUSED)
            cprintf("%d UNUSED  ",mlfq[lol].proc[it]->pid,mlfq[lol].proc[it]->state);
            else
            cprintf("%d OTHER  ",mlfq[lol].proc[it]->pid,mlfq[lol].proc[it]->state);
            if(it==mlfq[lol].num-1) cprintf("...%d\n",lol);
            }
        }

        c->proc = ptorun;
        switchuvm(ptorun);
        ptorun->state = RUNNING;
        ptorun->num_run++;
    
        swtch(&(c->scheduler), ptorun->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        
        // cprintf("%d in %d runtime %d ticks %d\n",ptorun->pid,ptorun->current_queue,ptorun->rtime,ptorun->ticksnow[ptorun->current_queue]);
        cprintf("selected %d from %d at %d\n",ptorun->pid,i,j);
        
        //first do all the checking,putting into new/same queue if reqd
        if(ptorun!=0 ){//has not been killed
        if(ptorun->killed==0){       
          int newq=i; 
          if(i<4 && 
          mlfq[i].rtime<=ptorun->ticksnow[ptorun->current_queue]) {  //did it exceed the time slice?
            newq=i+1;
          }

          //now putting process in the queue newq 
          mlfq[newq].proc[mlfq[newq].num]=ptorun;
          if(newq==i+1) {
            mlfq[newq].proc[mlfq[newq].num]->ticksnow[newq]=0;
            cprintf("%d shifted down to %d\n",ptorun->pid,newq);
          }
          mlfq[newq].proc[mlfq[newq].num]->intime=ticks;    //since time since it was waiting should reset
          mlfq[newq].proc[mlfq[newq].num++]->current_queue=newq;
        }
        }

        //now time to delete from the queue
        for(int it=j+1;it<mlfq[i].num;it++) mlfq[i].proc[it-1]=mlfq[i].proc[it];
        mlfq[i].num--;
        mlfq[i].proc[mlfq[i].num]=0;
        

        // for(int it=0;it<mlfq[i].num;it++) cprintf("(%d..%d)  ",mlfq[i].proc[it]->pid,it);
        // cprintf("\n");

      }
      release(&ptable.lock);
    }
    
  #endif
  #endif
  #endif
  #endif
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

int
getpinfo(struct proc_stat *p_proc, int pid){  //0 if successful, 1 if otherwise
  
  for(int i=0; i< NPROC; i++){
    struct proc p=ptable.proc[i];
    if(p.pid==pid){
      p_proc->pid=p.pid;
      p_proc->runtime=p.rtime;   
      p_proc->num_run=p.num_run;  
      // cprintf("%d %d\n",p.rtime,p.num_run);
      // cprintf("%d %d\n",p_proc->runtime,p_proc->num_run);
      return 0;
    }
  }
  return -1;//means no such pid
}

int 
setpriority(int pri, int pid)  //returns 0 if success;
{
  if(pri<0 || pri>100){
    cprintf("Invalid priority\n");
    return -1;
  }

  struct proc *p;
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->pid==pid){
      int oldpri=p->priority;
      p->priority=pri;
      release(&ptable.lock);
      return oldpri;
    }
  }

  release(&ptable.lock);
  cprintf("No such process\n");
  return -1;// means process not found
}

void 
ps(void){

  struct proc *p=0;
  
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE)
          continue;
        cprintf("pid=%d pri=%d\n",p->pid,p->priority);
      }
  return;
}
