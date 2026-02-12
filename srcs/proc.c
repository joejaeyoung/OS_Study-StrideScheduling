#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

/**
 * 우선순위 큐 관리 구조체
 */
struct proc_list {
  int proc_count;
  struct proc *head;
  struct proc *tail;
};

/**
 * ptable 구조체에 우선순위 큐 추가
 */
struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct proc_list p_queue;
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

/**
 * @brief pass값이 계속 증가해 발생하는 오버플로우를 방지한다.
 * @brief 1.PASS_MAX값을 넘기는 경우 프로세스 중 가장 작은 pass값을 0으로 설정하고 나머지 프로세스도 감소치만큼 감소시킨다.
 * @brief 2.pass 값들 중 가장 작은 값과 가장 큰값의 차이가 DISTNCE_MAX를 넘기지 않게 한다.
 */
void check_and_rebase() {
  uint min_pass;
  struct proc *p;
  #ifdef REBASE
    uint old_pass;
  #endif

  //1. Runnable한 프로세스가 없는 경우
  if (ptable.p_queue.head == 0) return ;

  //2. 우선순위 큐(pass, pid에 대해)로 관리해 max 바로 찾음
  // - 가장 Pass 값이 작은 프로세스를 0으로 만들고 pass를 감소시킴
  // - 감소 이후에도 pass 값 차이가 너무 크면 distance_max로 설정
  if (ptable.p_queue.tail->pass > PASS_MAX) {
    #ifdef REBASE
      cprintf("\nRebase Process Start\n\n");
    #endif

    //2-1. pass min 값
    min_pass = ptable.p_queue.head->pass;

    //2-2. 모든 프로세스의 pass 값을 Min-pass 만큼 감소
    p = ptable.p_queue.head;
    while (p) {
      #ifdef REBASE
        old_pass = p->pass;
      #endif
      p->pass -= min_pass;
      
      //2-3. distance_max 제한 적용
      if (p->pass > DISTANCE_MAX) {
        #ifdef REBASE
          cprintf("Process %d's pass is standardize from %d, with distance cutting, to %d\n", p->pid, old_pass, DISTANCE_MAX);
        #endif
        p->pass = DISTANCE_MAX;
      }
      else {
        #ifdef REBASE
          cprintf("Process %d's pass is standardize from %d, to %d\n", p->pid, old_pass, p->pass);
        #endif
      }
      p = p->next;
    }
    #ifdef REBASE
      cprintf("\nRebase Process End\n\n");
    #endif
  }
}

/**
 * @brief 디버깅 용 함수이며, 큐의 현재 상태를 출력한다.
 */
void print_queues(void)
{
  struct proc *p;
  
  cprintf("\n===== QUEUE STATE =====\n");
  p = ptable.p_queue.head;
  if (!p) {
    cprintf("EMPTY\n");
    return ;
  }
  while(p) {
    cprintf("[PID %d, PASS %d] -> ", p->pid, p->pass);
    p = p->next;
  }
  cprintf("NULL\n");
  cprintf("=======================\n");
}

/**
 * @brief 프로세스의 티켓 수를 설정하고 stride 값을 계산한다.
 * @brief end_tickts가 1 이상이면 프로세스 수명을 설정한다.
 * 
 * @param tickets - 프로세스가 보유할 티켓 수 (1 이상 STRIDE_MAX 이하)
 * @param end_tickets - 프로세스 종료까지의 틱 수 (1 이상일 때만 적용)
 * @return 성공 시 0, 실패 시 -1
 */
int sys_settickets() {
  int tickets, end_ticks;

  //0. 인자 불러오기 실패시 -1 반환
  if (argint(0, &tickets) < 0) return -1;
  if (argint(1, &end_ticks) < 0) return -1;

  //1. 첫 번째 인자 검사
  if (tickets < 1 || tickets > STRIDE_MAX) return -1;
  
  //2. 현재 프로세스의 정보에 해당하는 Proc 구조체 가져오기.
  struct proc *curproc = myproc();

  //3. 두 번째 인자 검사 및 end_ticks 설정
  if (end_ticks >= 1) {
    curproc->end_ticks = end_ticks;
  }

  //4. 프로세스의 stride 값을 갱신
  curproc->tickets = tickets;
  curproc->stride = STRIDE_MAX / tickets;
  return 0;
}


/**
 * @brief 큐가 처음 생성될 때 큐를 초기화한다.
 */
void init_qeueus() {
  ptable.p_queue.head = ptable.p_queue.tail = 0;
}

/**
 * @brief 우선순위 큐의 규칙에 맞춰 새로운 노드를 추가한다.
 * 
 * @param p 새로 추가할 프로세스 구조체 (막 runnable 상태로 변경된)
 */
void add_to_queue(struct proc *p) {
  struct proc_list *q = &ptable.p_queue;
  p->next = p->prev = 0;

  //1. empty queue
  if (q->head == 0) {
    q->head = q->tail = p;
    return ;
  }

  //2. 우선순위 비교 후 삽입
  for (struct proc *cur = q->head; cur != 0; cur = cur->next) {
    int is_higher = 0;
    if (p->pass < cur->pass) {
      is_higher = 1;
    }
    else if (p->pass == cur->pass && p->pid < cur->pid) {
      is_higher = 1;
    }

    if (is_higher) {
      // cur 앞에 삽입
      p->next = cur;
      p->prev = cur->prev;
      if (cur->prev) cur->prev->next = p;
      else q->head = p; //p가 새로운 head
      cur->prev = p;
      return ;
    }
  }

  //3. 우선 순위가 가장 낮으므로 tail에 삽입
  q->tail->next = p;
  p->prev = q->tail;
  q->tail = p;
}

/**
 * @brief 큐에서 프로세스를 제거하는 함수
 * 
 * @param p 제거할 프로세스 구조체 (상태가 runnable에서 다른 것으로 변경)
 */
void remove_from_queue(struct proc *p) {
  // 큐에 연결되어 있지 않다면 아무것도 하지 않음
  if (p->prev == 0 && p->next == 0 && ptable.p_queue.head != p) {
      return;
  }

  struct proc_list *q = &ptable.p_queue;

  // p의 이전 노드가 p의 다음 노드를 가리키도록 함
  if (p->prev) {
    p->prev->next = p->next;
  } else { // p가 head였을 경우, head를 p의 다음 노드로 변경
    q->head = p->next;
  }

  // p의 다음 노드가 p의 이전 노드를 가리키도록 함
  if (p->next) {
    p->next->prev = p->prev;
  } else { // p가 tail이었을 경우, tail을 p의 이전 노드로 변경
    q->tail = p->prev;
  }

  // p의 연결 정보를 완전히 초기화
  p->next = 0;
  p->prev = 0;
}

/**
 * @brief 다음에 실행할 프로세스를 반환한다.
 * 
 * @return 다음에 실행할 프로세스 구조체
 */
struct proc *get_next_proc() {
  struct proc *tmp;
  tmp = ptable.p_queue.head;
  if (tmp) return tmp;
  return 0;
}

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  //처음 초기화 될 때, 큐도 같이 초기화 한다.
  init_qeueus();
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

  //proc 구조체 stride 관련 변수 초기화
  // p->tickets;
  p->stride = 0;
  p->pass = 0;
  p->ticks = 0;
  p->end_ticks = -1;

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
  //proc 구조체 stride 관련 변수 초기화
  p->stride = 0;
  p->pass = 0;
  p->ticks = 0;
  p->end_ticks = -1;
  p->prev = p->next = 0;

  //프로세스가 RUNNABLE상태로 변경되어 큐에 추가해서 관리한다.
  add_to_queue(p);

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

  np->state = RUNNABLE;
  //proc 구조체 stride 관련 변수 초기화
  np->stride = 0;
  np->pass = 0;
  np->ticks = 0;
  np->end_ticks = -1;
  np->prev = np->next = 0;

  //프로세스가 RUNNABLE상태로 변경되어 큐에 추가해서 관리한다.
  add_to_queue(np);

  release(&ptable.lock);

  /* DEBUG : 프로세스 생성 시점에 디버깅문 출력 */
  if (np->pid > 2 && np->parent->pid > 2)
    cprintf("Process %d start\n", np->pid);

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

  /* DEBUG : 프로세스 종료 시점에 디버깅문 출력 */
  if (curproc->pid > 2 && curproc->parent->pid > 2) {
    cprintf("Process %d exit\n", curproc->pid);
    
    // // 충분한 지연으로 출력 버퍼 플러시 보장
    // volatile int delay;
    // for(delay = 0; delay < 2000000; delay++) {
    //   // 더 긴 지연으로 확실한 출력 순서 보장
    // }
  }
  #ifdef DEBUG
    cprintf("Not Printing_ pid : %d, ppid : %d, pname : %s\n", curproc->pid, curproc->parent->pid, curproc->name);
  #endif

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

    //오버 플로우 체크 및 rebase
    check_and_rebase();

    //다음 프로세스를 뽑아 가져옵니다.
    p = get_next_proc();

    if (p) {
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      //프로세스가 RUNNING 상태로 변경되어, 큐에서 제거한다.
      remove_from_queue(p);

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
  //프로세스가 다시 RUNNABLE상태로 변경되어 큐에 추가한다.
  add_to_queue(myproc());
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
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      //프로세스가 다시 RUNNABLE상태로 변경되어 큐에 추가한다.
      add_to_queue(p);
    }
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
      if(p->state == SLEEPING) {
        p->state = RUNNABLE;
        //프로세스가 다시 RUNNABLE상태로 변경되어 큐에 추가한다.
        add_to_queue(p);
      }
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
