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
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/float.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. : stack overflow 감지용 */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. 

   THREAD_READY 상태의 프로세스 목록 
*/
static struct list all_list;
static struct list ready_list;
static struct list sleep_list;
// static struct list donations;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIMER_FREQ 100
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. : 마지막 양보후 타이머틱 수*/

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

#define load_fir_co divide_xbyn (convert_ntox (59), 60)
#define load_sec_co divide_xbyn (convert_ntox (1), 60)

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. 
   ===================뭐래는겨============================
 */

#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

// ----------------------------------------------------------
#define f (1 << 14)

const int convert_ntox (const int n) {
   return n*f;
}
const int convert_xton (const int x) {
    // rounding to nearest
    if (x>=0) return (x+f/2)/f;
    else return (x-f/2)/f;
}

const int add_xandn (const int x, const int n) {
    return x+n*f;
}
const int add_xandy (const int x, const int y) {
    return x+y;
}

const int sub_nfromx (const int x, const int n) {
    return x-n*f;
}
const int sub_yfromx (const int x, const int y) {
    return x-y;
}

const int mult_xbyn (const int x, const int n) {
    return x * n;
}
const int mult_xbyy (const int x, const int y) {
    return ((int64_t)x)*y/f;
}

const int divide_xbyn (const int x, const int n) {
    return x/n;
}
const int divide_xbyy (const int x, const int y) {
    return ((int64_t)x)*f/y;
}

// ----------------------------------------------------------

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. 

   main()에서 스레드 시스템을 초기화하기 위해 호출된다. 
   주요 목적은 pintos의 초기 스레드를 위한 struct thread를 생성하는것 

   thread_init()이 실행되기 전 thread_current() 실패할 것 => 스레드의 매직 값 설정된 상태가 아니라서
   thread_current()가 직간접적으로 많이 쓰이니까 초기화단계 꼭 필요해유
*/
void
thread_init (void) {
	/* 현재 인터럽트 비활성화 확인 */
	ASSERT (intr_get_level () == INTR_OFF); 

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context : 자료구조 초기화 */
	lock_init (&tid_lock);
	list_init (&all_list);
	list_init (&ready_list);
	list_init (&destruction_req);
	list_init (&sleep_list);
	// list_init (&donations);

	load_avg = 0;

	/* Set up a thread structure for the running thread.: 현재 실행중인 스레드 정보 설정 */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. 
   
   idle스레드를 생성하고 인터럽트 활성화->스케줄러 활성화(스케줄러는 타이머 인터럽트에서 반환될때 실행됨. 이때 intr_yield_on_return() 사용)
*/
void
thread_start (void) {
	/* Create the idle thread.*/
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started); // PRI_MIN = 0

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. 

   타이머 인터럽트핸들러가 각 타이머틱마다 호출하는 함수
   타이머 인터럽트가 발생할때마다 실행 함수
   스레드 통계를 추적하고 타임 슬라이드가 만료되면 스케줄러를 트리거한다.
*/
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
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
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.
   새로운 커널 스레드를 생성하고 ready 큐에 추가하는 함수

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   thread_start()를 호출하면 thread_create()가 반환되기전에 만들어진 스레드가 스케줄링되거나 끝날수있다.
   thread_start() 호출하지 않았다면 원래 스레드가 계속 진행

   thread_create()함수는 스레드의 struct thread 및 스택을 위한 페이지를 할당하고 초기화 후
   가짜 스택 프레임을 설정한다. 스레드는 차단된 상태로 초기화되며, 반환 직전에 차단 해제되어 
   새로운 스레드가 스케줄될 수 있도록 함.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) { //이름,우선순위,인자
		                                    //함수(쓰레드가 실행할 함수 => 쓰레드가 실행될때 함수 실행되고 함수종료되면 쓰레드 종료, main()처럼 작동한다.)
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	t->fdt = palloc_get_multiple(PAL_ZERO,FDT_PAGES);
	if(t -> fdt  == NULL)
		return TID_ERROR;
	t->fd_index = 2; // 0은 stdin 1은 stdout
	t->fdt[0] = 1;
	t->fdt[1] = 2;
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. 
	우선 순위 비교해준다음에 넣어주는 코드 만들어주기
	*/ 
	thread_unblock (t);
	thread_compare_priority();
	list_push_back(&thread_current()->child_list, &t->child_elem);

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   ready-to-run상태로 만드는 함수 : 만약 블록이 안되어있다면 에러임  

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED); 
	// list_push_back (&ready_list, &t->elem);
	list_insert_ordered(&ready_list,&t->elem,cmp_priority,NULL);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.: 실행중인 스레드 반환
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
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
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	list_remove(&thread_current()->all_elem);
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. 

   CPU를 스케줄러에 양보한다-> 스케줄러가 새로운 스레드를 선택할 수 있도록한다.
   현재 스레드는 스레드 슬립 상태로 들어가지 않고 바로 다시 스케줄러에의해 실행 될수있다.
   새로운 스레드가 현재 실행중인 스레드일수도 있기때문에 특정길이 동안 스레드 실행 보장은 못한다.
*/
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		//list_push_back (&ready_list, &curr->elem);
		list_insert_ordered(&ready_list,&curr->elem,cmp_priority,NULL);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	if(thread_mlfqs)
		return;
	thread_current ()->init_priority = new_priority;

	re_priority();
	thread_compare_priority();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) {
	thread_current ()->nice = nice;
	calculate_priority(thread_current());
	thread_compare_priority();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	return convert_xton (mult_xbyn (load_avg, TIMER_FREQ));
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	return convert_xton (thread_current ()->recent_cpu) * TIMER_FREQ;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. 
   
   idle thread는 다른 쓰레드가 실행 준비가 되지 않은 경우 실행 
   (CPU에 아무런 작업이 없을 때 실행되는 특별 스레드-> 아무작업 수행안하고 대기)
   thread_start()의해 초기 준비 목록에 넣어진다.
   초기에 한 번 스케줄 되고 thread_start()가 진행 될수 있도록 세마포어를 up(잠금 풀어주고) 하고  idle thread 즉시 블록 
   이후에는 idle thread는 준비목록에 나타나지 않는다.
   next_thread_to_run()의해 반환 된다.
   
*/
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	// idle_thread->recent_cpu = 0;
	sema_up (idle_started);

	for (;;) {
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
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
	t->init_priority = priority;
	t->wait_on_lock = NULL;
	t->nice = 0;
	t->recent_cpu = 0;
	t-> runn_file= NULL;
	// t->recent_cpu = thread_current ()->recent_cpu;
	list_init (&t->donations);
	list_push_back (&all_list, &t->all_elem);

	//project 2
	list_init (&t->child_list);
	sema_init(&t->wait_sema,0);
	sema_init(&t->free_sema,0);
	sema_init(&t->load_sema,0);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. 
   스케줄 할 스레드를 선택하고 반환하는 역할 
   ready_list에서 스레드 선택해서 반환하는데 비었는데 현재 실행중인 스레드가 있다면 현재 실행중인 스레드 반환
   아니면 idle_thread반환
*/
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. 
   
   =====이해 할 필요 없는 함수====
   스레드를 전환하는 과정
   현재 실행중인 스레드의 상태를 저장하고 전환중인 스레드의 상태를 복원
   새로운 스레드는 이미 실행중이며 인터럽트는 아직 비활성화 된 상태 

   함수 실행시 전환 진행중이니 printf는 함수의 끝부분에 할것
*/
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). 
 
 * do_schedule : 스케줄링 전 현재 스레드의 상태를 변경하고 다음 실행 할 스레드를 찾아 schedule 실행
 * schedule : 현재 실행중인 스레드와 다음 실행할 스레드가 다른 경우 다음스레드로 전환
 *            전환 후 새로운 타임 슬라이스를 시작
 *            사용자 모드인 경우 새로운 주소공간 활성화, 이전 스레드 종료중이면 해당 스레드 자원 정리
 
*/
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

void thread_sleep(int64_t ticks){
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	// ASSERT (!intr_context ());

	old_level = intr_disable ();

	if (curr != idle_thread){
		curr->awake = ticks;
		list_push_back (&sleep_list, &curr->elem);
		thread_block();
	}
	intr_set_level (old_level);
}

void thread_awake(int64_t ticks){

	struct list_elem *curr = list_begin(&sleep_list);
	while(curr != list_tail(&sleep_list)) {
		struct thread *t = list_entry(curr,struct thread,elem);
		if (t->awake <= ticks) {
			curr=list_remove(curr);
			thread_unblock(t);
		} else {
			curr = list_next(curr);
		}
	}
}

bool cmp_priority (const struct list_elem *a,const struct list_elem *b,void *aux){
	int a_priority = list_entry(a,struct thread,elem) -> priority;
	int b_priority = list_entry(b,struct thread,elem) -> priority;
	return a_priority > b_priority;
} 

void thread_compare_priority(void){
	struct thread *curr = thread_current();

	if (!list_empty (&ready_list) && 
    thread_current ()->priority < 
    list_entry (list_front (&ready_list), struct thread, elem)->priority)
        thread_yield ();

}

bool cmp_donation_priority (const struct list_elem *a,const struct list_elem *b,void *aux){
	int a_priority = list_entry(a,struct thread,d_elem) -> priority;
	int b_priority = list_entry(b,struct thread,d_elem) -> priority;
	return a_priority > b_priority;
} 

void priority_donation(void){
	int depth;
	struct thread *curr = thread_current();
	for(depth = 0; depth<8; depth++){
		if(!curr->wait_on_lock) break;
		struct thread *holder =  curr->wait_on_lock->holder;
		holder->priority = curr->priority;
		curr = holder;
	}
}

void remove_donation(struct lock *lock){
	struct list_elem *e;
	struct thread *curr = thread_current();
	for(e = list_begin(&curr->donations);e != list_end(&curr->donations); e = list_next(e)){
		struct thread *t = list_entry(e,struct thread,d_elem);
		if(t->wait_on_lock == lock){
			list_remove(&t->d_elem);
		}
	}
}

void re_priority(void){
	struct thread *curr = thread_current();
	curr->priority =  curr->init_priority;
	
	if(!list_empty(&curr->donations)){
		list_sort(&curr->donations,cmp_donation_priority,NULL);
		struct thread *front = list_entry(list_front(&curr->donations),struct thread,d_elem);
		
		if(curr->priority < front->priority)
			curr->priority = front->priority;
	}
}

void calculate_priority (struct thread *t) {
	if(t == idle_thread)
		return;
	t->priority = convert_xton(add_xandn(divide_xbyn(t->recent_cpu,-4),63-t->nice*2));
}

void calculate_recent_cpu (void) {
	if (thread_current () != idle_thread) {
		int recent_cpu = thread_current ()->recent_cpu;
		recent_cpu = add_xandn (recent_cpu, 1);
		thread_current()->recent_cpu = recent_cpu;
	}
}

void calculate_load_avg (void) {
	int ready_threads = 0;

	if (thread_current () == idle_thread)
		ready_threads = list_size (&ready_list);
	else {
		// + 1 is count for the running thread
		ready_threads = list_size (&ready_list) + 1;
	}

	load_avg = mult_xbyy (load_fir_co, load_avg) + mult_xbyn (load_sec_co,  ready_threads);
}

void recalculate_recent_cpu (void) {
	ASSERT (!list_empty (&all_list));

	struct list_elem* curr;
	struct thread* t;

	curr = list_begin(&all_list);
	
	while (curr != list_tail(&all_list)) {
		t = list_entry(curr, struct thread, all_elem);
		t->recent_cpu = mult_xbyy (divide_xbyy (mult_xbyn (load_avg, 2), mult_xbyn (load_avg, 2) + convert_ntox(1)), t->recent_cpu) + convert_ntox(t->nice);
		curr = list_next(curr);
	}
}
void recalculate_priority (void) {
	ASSERT (!list_empty (&all_list));

	struct list_elem* curr;
	struct thread* t;

	curr = list_begin(&all_list);
	
	while (curr != list_tail(&all_list)) {
		t = list_entry(curr, struct thread, all_elem);
		calculate_priority(t);
		curr = list_next(curr);
	}
}
