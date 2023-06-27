#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "threads/fixed-point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Limit on depth of nested priority donation */
#define DEPTH_LIMIT 8

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;
static int64_t load_avg;                    /* System load average */
static struct list sleeping_thread_list;    /* List of sleeping threads */
  
static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

void sort_ready_list_priority(void);
bool check_current_thread_priority_against_ready(void);

/* Alarm clock functions*/
bool compare_thread_awake_time (const struct list_elem *a, 
                               const struct list_elem *b, void* aux);

/* Priority donation functions */
void calculate_thread_effective_priority (void);

/* Advanced scheduler functions */
void calculate_thread_load_avg (void);
void increment_thread_recent_cpu (void);
void calculate_recent_cpu_for_all (void);
void calculate_thread_recent_cpu (struct thread *t, void *aux);
void calculate_thread_priority_for_all (void);
void calculate_thread_priority (struct thread *t, void *aux);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init(&sleeping_thread_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();

  if (thread_mlfqs) {
    load_avg = int_to_fp(0); // Initialize load_avg to 0 if mlfqs is true
  }
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  if (thread_mlfqs) {
    if (t->priority > thread_current()->priority) thread_yield();
  } else {
    if (t->effective_priority > thread_current()->effective_priority) {
      thread_yield();
    }
  }

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  //priority scheduling so insert into ready list based off priority
  list_insert_ordered(&ready_list, &t->elem, compare_thread_priority, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
  //since exited process, allow parent to continue
  sema_up(&thread_current()->wait_child_sema);
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (thread_current() != idle_thread) 
    //priority scheduling so insert into ready list based off priority
    list_insert_ordered(&ready_list, &thread_current()->elem, 
                        compare_thread_priority, NULL);
  thread_current()->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  if (thread_mlfqs) return;
  enum intr_level old_level;

  thread_current()->priority = new_priority;

  old_level = intr_disable();

  calculate_thread_effective_priority();

  if(check_current_thread_priority_against_ready()) {
    thread_yield();
  } 

  intr_set_level(old_level);
  return;
}


/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  if (thread_mlfqs) return thread_current()->priority;
  else return thread_current ()->effective_priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{ 
  // Sets the current thread's nice value to the new nice value
  thread_current ()->nice = nice;
  // Updates the thread's priority according to the new nice value
  calculate_thread_priority(thread_current(), NULL);

  /* If new priority leads to current thread being not 
     the highest priority, block it */
  if(check_current_thread_priority_against_ready()) {
    thread_yield();
  } 
  return;
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return fp_to_int_rounded(load_avg * 100);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return fp_to_int_rounded(thread_current()->recent_cpu * 100);
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;

  if(thread_mlfqs) {
    // If current thread is the main thread, make nice 0 
    if(t == initial_thread) {
      t->nice = 0;
      t->recent_cpu = 0;
    // Inherits nice and recent_cpu values from parent
    } else {
      t->nice = thread_current()->nice;
      t->recent_cpu = thread_current()->recent_cpu;
    }
    calculate_thread_priority(t, NULL);
  } else {
    t->effective_priority = priority;
    list_init(&t->owned_locks);
    t->blocking_lock = NULL;
  } 

  #ifdef USERPROG
  sema_init(&t->wait_child_sema, 0);
  list_init(&t->child_processes);
  t->exit_code = -1;
  t->curr_fd = 3;
  list_init(&t->opened_files);
  #endif

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}


/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    //ready list sorted in order of ascending priority so pop back
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Checks if thread A's sleep tick is less than thread B's sleep tick*/
bool 
compare_thread_awake_time (const struct list_elem *a, 
                           const struct list_elem *b, void* aux) {
  /* Convert list elements into respective thread */
  struct thread *thread_a = list_entry(a, struct thread, elem);
  struct thread *thread_b = list_entry(b, struct thread, elem);

  (void)aux;

  /* Return a boolean comparator of the two threads' awake_time values */
  return thread_a->awake_time < thread_b->awake_time;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

/* Compares each thread's priority for insertion */
bool 
compare_thread_priority(const struct list_elem *a, const struct list_elem *b,
                        void* aux) {
  /* Convert list elements into respective thread */
  struct thread *thread_a = list_entry(a, struct thread, elem);
  struct thread *thread_b = list_entry(b, struct thread, elem);

  (void)aux;

  /* Return a boolean comparator of the two threads' awake_time values */
  if (thread_mlfqs) {
    return thread_a->priority > thread_b->priority;
  } else {
    return thread_a->effective_priority > thread_b->effective_priority;
  }
}

/* Sort the ready list */
void
sort_ready_list_priority(void) {
  if (!list_empty(&ready_list))
    list_sort(&ready_list, compare_thread_priority, NULL);
}

/* Check if current thread's priority is less than the highest 
   priority thread in ready list */
bool
check_current_thread_priority_against_ready(void) {
  return compare_thread_priority(list_begin(&ready_list), 
                                 &thread_current()->elem, NULL);
}

/* Alarm Clock Functions */

void
thread_sleep(int64_t ticks, int64_t start) {
  thread_current()->awake_time = start + ticks;
  
  /* Disables interrupts to avoid concurrency issues */
  enum intr_level old_status = intr_disable();
  /* Insert current thread into a list of blocked threads 
     sorted by awake_time value */
  list_insert_ordered(&sleeping_thread_list, &thread_current()->elem,
                      compare_thread_awake_time, NULL);
  /* Block the current thread */
  thread_block();
  /* Enable interrupts back */
  intr_set_level(old_status);
}

/* Wakes all sleeping threads that need to wake up */
void 
awaken_threads(int64_t ticks) {
  // Check through blocked lists if any thread is ready to wake up
  while(!list_empty(&sleeping_thread_list)) {
    // Gets the element from the head of the list and converts it to a thread
    struct thread *thread = list_entry(list_front(&sleeping_thread_list), 
                                                  struct thread, elem);
    /* If the thread's awake_time is less than 
       the current ticks, wake up thread due to alarm */
    if(thread->awake_time <= ticks) {
      list_pop_front(&sleeping_thread_list);
      thread_unblock(thread);
    } else {
      /* If the head's thread is not ready to wake up, 
         every element after must also be not ready
         to wake up due to being sorted by awake_time */
      break;
    }
  }
}

/* Priority Donation Functions */

/* Handle lock fields after being acquired */
void
handle_lock_acquire(struct lock *lock) {
  lock->holder->blocking_lock = NULL;
  /* Add lock to the current thread's owned_locks list */
  list_push_back(&thread_current()->owned_locks, &lock->elem);
}

/* Handle priority donations when lock blocks other threads */
void
handle_lock_block(struct lock *lock) {
  thread_current()->blocking_lock = lock;
  struct thread *blocking_thread = lock->holder;

  /* Iterate until the DEPTH_LIMIT or there's no longer a blocking thread */
  for (int i = 0; i < DEPTH_LIMIT && blocking_thread != NULL; i++) {
    if (blocking_thread->effective_priority 
        < thread_current()->effective_priority) {
      blocking_thread->effective_priority = 
        thread_current()->effective_priority;
      /* Check if the blocking thread is in the ready list */
      if (blocking_thread->status == THREAD_READY) {
        sort_ready_list_priority();
        break;
      }
    }
    if(blocking_thread->blocking_lock != NULL) {
        /* Go one blocking thread deeper */
        blocking_thread = blocking_thread->blocking_lock->holder;
    } else {
      break;
    }
  }
  return;
}

/* Handle priority donations when lock is released */
void
handle_lock_release(struct lock *lock) {
  struct lock *cur_lock;
  /* Iterate through owned locks */
  for (struct list_elem *lock_it = list_begin(&thread_current()->owned_locks); 
       lock_it != list_end(&thread_current()->owned_locks); 
       lock_it = list_next(lock_it)) {
    cur_lock = list_entry(lock_it, struct lock, elem);
    if(cur_lock == lock){
      //if on lock that removing, remove it
      list_remove(lock_it);
      break;
    }
  }
  calculate_thread_effective_priority();
  return;
}

/* Update the current thread's effective_priority */
void
calculate_thread_effective_priority (void) {
  struct lock *cur_lock;
  int cur_priority = PRI_MIN;
  thread_current()->effective_priority = thread_current()->priority;

  /* Iterate through owned locks */
  for (struct list_elem *lock_it = list_begin(&thread_current()->owned_locks); 
       lock_it != list_end(&thread_current()->owned_locks); 
       lock_it = list_next(lock_it)) {
    cur_lock = list_entry(lock_it, struct lock, elem);
    cur_priority = list_entry(list_max(&cur_lock->semaphore.waiters, 
                                       compare_thread_priority, NULL), 
                              struct thread, elem)->effective_priority;
    /* Update effective priority if higher priority 
       is found in waiters for the lock */
    if (cur_priority > thread_current()->effective_priority) {
      thread_current()->effective_priority = cur_priority;
    }
  }
  return;
}

/* Advanced Scheduler Functions */

/* Calculates a thread's load average using the formula: 
   load_avg = (59/60)*load_avg + (1/60)*ready_threads, 
   this estimates the average number of threads ready 
   to run over the past minute */
void
calculate_thread_load_avg(void) {
  // Idle thread shouldn't count as a ready/running thread
  int num_running_threads = thread_current() == idle_thread
                                ? list_size(&ready_list)
                                : list_size(&ready_list) + 1;
  // Kept all values as fixed point to avoid rounding errors
  load_avg = multiply_fp(divide_fp(int_to_fp(1), int_to_fp(60)),
                         int_to_fp(num_running_threads)) +
             multiply_fp(divide_fp(int_to_fp(59), int_to_fp(60)), load_avg);
}

/* Increments the thread's recent cpu value by 1 */
void
increment_thread_recent_cpu(void) {
  if (thread_current() != idle_thread)
    thread_current()->recent_cpu = thread_current()->recent_cpu + int_to_fp(1);
}

/* Calculates the recent cpu for all threads */
void
calculate_recent_cpu_for_all(void) {
  thread_foreach(calculate_thread_recent_cpu, NULL);
  return;
}

/* Calculates a thread's recent cpu value using the formula:
   (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice,
   which measures how much CPU time this process has received "recently" */
void
calculate_thread_recent_cpu(struct thread *t, void *aux) {
  if (t == idle_thread) return;

  t->recent_cpu =
      multiply_fp(divide_fp(multiply_fp(int_to_fp(2), load_avg),
                            multiply_fp(int_to_fp(2), load_avg) 
                            + int_to_fp(1)),
                  t->recent_cpu) +
      int_to_fp(t->nice);

  (void)aux;
  return;
}

/* Calculates the thread's priority for all threads */
void
calculate_thread_priority_for_all(void) {
  thread_foreach(calculate_thread_priority, NULL);
  return;
}

/* Calculates a single thread's priority */
void
calculate_thread_priority(struct thread *t, void *aux) {
  if (t == idle_thread) return;

  int priority = fp_to_int_truncated(
      int_to_fp(PRI_MAX) - divide_fp(t->recent_cpu, int_to_fp(4)) -
      multiply_fp(int_to_fp(t->nice), int_to_fp(2)));

  // Check if priority is within bounds
  if (priority > PRI_MAX) priority = PRI_MAX;
  else if (priority < PRI_MIN) priority = PRI_MIN;
  
  t->priority = priority;

  (void)aux;
  return;
}

/* Handle updates to niceness, recent_cpu, and load_avg*/
void
handle_mlfqs(int64_t ticks) {
  // Only run if mlfqs is enabled
  if (!thread_mlfqs) return;
  increment_thread_recent_cpu();
  // Every second
  if (ticks % TIMER_FREQ == 0) {
    calculate_thread_load_avg();
    calculate_recent_cpu_for_all();
    calculate_thread_priority_for_all();
  }
  // Every 4 ticks
  if (ticks % TIME_SLICE == 0) {
    calculate_thread_priority(thread_current(), NULL);
    // Checks if changing the current thread affects which thread should run
    if (check_current_thread_priority_against_ready()) {
      intr_yield_on_return();
    }
  }
}

/*Function used to get child thread with tid id from t's list of child threads
if no such thread exists, return NULL*/
struct thread* find_child_from_id (struct thread* t, tid_t id){
  //check if list is empty
  if(list_empty(&t->child_processes))
    return NULL;
  //iterate throuh list looking for thread
  struct thread* curr_thread;
  struct list_elem* e;
  for (e = list_begin (&t->child_processes); 
  e != list_end (&t->child_processes); e = list_next (e)){
      //if find thread return
      curr_thread = list_entry(e, struct thread, child_elem);
      if(curr_thread->tid == id)
        return curr_thread;
    }
  return NULL;
}

/*Function used to get thread with tid id from the list of all threads
if no such thread exists, return NULL*/
struct thread* find_thread_from_id (tid_t id){
  struct thread* curr_thread;
  struct list_elem* e;
  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e)){
      //if find thread return
      curr_thread = list_entry (e, struct thread, allelem);
      if(curr_thread->tid == id)
        return curr_thread;
    }
  return NULL;
}