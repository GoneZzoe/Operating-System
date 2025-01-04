#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include <stdio.h>
#include "malloc369.h"
#include "interrupt.h"

/* This is the wait queue structure, needed for Assignment 2. */ 
struct wait_queue {
	/* ... Fill this in Assignment 2 ... */
	struct wait_node* waitHead;
	struct wait_node* waitTail;
};

typedef struct wait_node {
	struct wait_node* next;
	Tid tid;
} wait_node;

void enqueue_wait(Tid tid, struct wait_queue* wq)
{
	if (wq->waitHead == NULL && wq->waitTail == NULL)
	{
		wq->waitHead = malloc369(sizeof(wait_node));
		wq->waitHead -> next = NULL;
		wq->waitHead -> tid = tid;
		wq->waitTail = wq->waitHead;
	}
	else
	{
		wait_node* temp = malloc369(sizeof(wait_node));
		temp -> tid = tid;
		temp -> next = NULL;
		wq->waitTail -> next = temp;
		wq->waitTail = temp;
	}
}

Tid dequeue_wait(struct wait_queue* wq)
{
	if (wq->waitHead == NULL && wq->waitTail == NULL) return THREAD_NONE;

	wait_node* temp = wq->waitHead;
	int rel = temp -> tid;
	wq->waitHead = wq->waitHead -> next;
	if (wq->waitHead == NULL) wq->waitTail = NULL;
	free369(temp);
	return rel;
}

/* For Assignment 1, you will need a queue structure to keep track of the 
 * runnable threads. You can use the tutorial 1 queue implementation if you 
 * like. You will probably find in Assignment 2 that the operations needed
 * for the wait_queue are the same as those needed for the ready_queue.
 */

/* This is the thread control block. */
typedef struct thread {
	/* ... Fill this in ... */
	Tid tid;
    int state;
    void* stack_bottom;
    ucontext_t mycontext;
	struct wait_queue* wq;
	// int exit_code;
}thread;

typedef enum thread_state {
    RUNNING = -10,
    READY = -11,
    DYING = -12,
	SLEEP = -13,
	ZOMBIE = -14,
} thread_state;

typedef struct ready_node{
    struct ready_node* next;
    Tid tid;
}ready_queue;

ready_queue* readyHead = NULL;
ready_queue* readyTail = NULL;

Tid cur_tid = 0;
// global array of thread pointer. pointer is easily to set up value, delete and require less state 
// after trying to implement statically thread array.
thread* thread_pool[THREAD_MAX_THREADS] = {NULL};
int exit_arr [THREAD_MAX_THREADS] = {0};

void enqueue(Tid tid)
{
	if (readyHead == NULL && readyTail == NULL)
	{
		readyHead = malloc369(sizeof(ready_queue));
		readyHead -> next = NULL;
		readyHead -> tid = tid;
		readyTail = readyHead;
	}
	else
	{
		ready_queue* temp = malloc369(sizeof(ready_queue));
		temp -> tid = tid;
		temp -> next = NULL;
		readyTail -> next = temp;
		readyTail = temp;
	}
}

Tid dequeue()
{
	if (readyHead == NULL && readyTail == NULL) 
	{
		return THREAD_NONE;
	}
	ready_queue* temp = readyHead;
	int rel = temp -> tid;
	readyHead = readyHead -> next;
	if (readyHead == NULL) readyTail = NULL;
	free369(temp);
	return rel;
}

int remove_from_queue(Tid tid)
{
	ready_queue* cur = readyHead;
	ready_queue* prev = NULL;

	while (cur != NULL)
	{
		if (cur -> tid == tid)
		{
			if (prev == NULL)
			{
				readyHead = cur -> next; // deleting head.
			} 
			else
			{
				prev -> next = cur -> next; // deleting other.
			}
			if (cur == readyTail) readyTail = prev; // if deleing the tail.
			free369(cur);
			return 0;
		}
		else
		{
			prev = cur;
			cur = cur -> next;
		}
	}
	return -1;
}

Tid find_spot()
{
	int t = -1;
	for (int i = 0; i < THREAD_MAX_THREADS; ++ i)
    {
        if (thread_pool[i] == NULL || thread_pool[i]->state == DYING)
        {
            t = i;
            break;
        }
    }
	return t;
}

void clean_zombies()
{
	for (int i = 0; i < THREAD_MAX_THREADS; i ++)
    {
        if (thread_pool[i] != NULL && thread_pool[i]->state == DYING && i != cur_tid)
        {
			wait_queue_destroy(thread_pool[i]->wq);
			free369(thread_pool[i]->stack_bottom);
            free369(thread_pool[i]);
            thread_pool[i] = NULL;
        }
    }
}

/**************************************************************************
 * Assignment 1: Refer to thread.h for the detailed descriptions of the six
 *               functions you need to implement. 
 **************************************************************************/

void
thread_init(void)
{
	/* Add necessary initialization for your threads library here. */
        /* Initialize the thread control block for the first thread */
    thread* t = (thread *)malloc369(sizeof(thread));
	t->tid = 0;
    t->state = RUNNING;
    t->stack_bottom = NULL;
	t->wq = NULL;
	getcontext(&t->mycontext);
    cur_tid = t->tid;
    thread_pool[t->tid] = t;
	// t->exit_code = -50;
}

Tid
thread_id()
{
	return cur_tid;
}


/* New thread starts by calling thread_stub. The arguments to thread_stub are
 * the thread_main() function, and one argument to the thread_main() function. 
 */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
		interrupts_on();
		thread_main(arg); // call thread_main() function with arg
        thread_exit(0);
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
	bool sig_enable = interrupts_off();
	// before finding avaliable spot, clean out zombies.
    // clean_zombies();
    // find an available spot.
    Tid t = find_spot();
    if (t == -1) 
	{
		interrupts_set(sig_enable);
		return THREAD_NOMORE;
	}
	// malloc necessary space.
    void* s_ptr = malloc369(THREAD_MIN_STACK);
    if (!s_ptr) 
	{
		interrupts_set(sig_enable);
		return THREAD_NOMEMORY;
	}

	thread * th = (thread *)malloc369(sizeof(thread));
	thread_pool[t] = th;

	th->tid = t;
	th->state = READY;
	th->stack_bottom = s_ptr;
	th->wq = NULL;
	// th->exit_code = -50;
	getcontext(&th->mycontext);
	// getting current context and modifiy registers.
	th->mycontext.uc_mcontext.gregs[REG_RIP] = (greg_t) &thread_stub;
	th->mycontext.uc_mcontext.gregs[REG_RDI] = (greg_t) fn;
	th->mycontext.uc_mcontext.gregs[REG_RSI] = (greg_t) parg;
	th->mycontext.uc_mcontext.gregs[REG_RSP] = (greg_t) (th->stack_bottom + THREAD_MIN_STACK - 8);
	enqueue(t);
	interrupts_set(sig_enable);
	return t;
}

Tid
thread_yield(Tid want_tid)
{
	bool enabled = interrupts_off();
	if (want_tid < -2 || want_tid >= THREAD_MAX_THREADS || (want_tid > 0 && (thread_pool[want_tid] == NULL || thread_pool[want_tid]->state == DYING)))
	{
		interrupts_set(enabled);
		return THREAD_INVALID;
	}

	if (want_tid == THREAD_SELF || want_tid == cur_tid)
	{
		interrupts_set(enabled);
		return cur_tid;
	}

	if (want_tid == THREAD_ANY)
	{
		want_tid = dequeue();
		if (want_tid == THREAD_NONE) 
		{
			interrupts_set(enabled);
			return THREAD_NONE;
		}

		while (thread_pool[want_tid] == NULL || thread_pool[want_tid]->state == DYING)
		{
			want_tid = dequeue();
			if (want_tid == THREAD_NONE) 
			{
				interrupts_set(enabled);
				return THREAD_NONE;
			}
		}
	}
	int setcontext_called = 0;
	// put cur to sleep and alter TCB.
	if (thread_pool[cur_tid]->state == RUNNING)
	{
		thread_pool[cur_tid]->state = READY;
		enqueue(cur_tid);
	}
	// save current context for future resume.
	getcontext(&thread_pool[cur_tid]->mycontext);
	// kill zombie thread if yield is called from exit.
	clean_zombies();
	if (setcontext_called) 
	{
		// if (thread_pool[cur_tid]->state == ZOMBIE) {
		// 	thread_exit(thread_pool[cur_tid]->exit_code);
		// }
		interrupts_set(enabled);
		return want_tid;
	}
	
	setcontext_called = 1;
	remove_from_queue(want_tid);
	cur_tid = want_tid;
	thread_pool[cur_tid]->state = RUNNING;
	//restore the wanted context.
	setcontext(&thread_pool[cur_tid]->mycontext);

	return THREAD_FAILED;
}

void
thread_exit(int exit_code)
{
	bool sig_enable = interrupts_off();
	// also messed up cleaning process.
	thread_pool[cur_tid]->state = DYING;
	remove_from_queue(cur_tid);
	exit_arr[cur_tid] = exit_code;
	if (thread_pool[cur_tid]->wq != NULL)
	{
		while (thread_pool[cur_tid]->wq->waitHead != NULL)
		{
			Tid tid = dequeue_wait(thread_pool[cur_tid]->wq);
			enqueue(tid);
		}
	}
	if (readyHead == NULL) {
		exit(exit_code);
	}
	interrupts_set(sig_enable);
	thread_yield(THREAD_ANY);
}

Tid
thread_kill(Tid tid)
{
	bool sig_enable = interrupts_off();
	if (tid < -2 || tid >= THREAD_MAX_THREADS || (tid > 0 && (thread_pool[tid] == NULL || thread_pool[tid]->state == DYING)))
	{
		interrupts_set(sig_enable);
		return THREAD_INVALID;
	}
	if (cur_tid == tid) 
	{
		interrupts_set(sig_enable);
		return THREAD_INVALID;
	}
	// messup my clean function dont know how to fix.
	thread_pool[tid]->state = DYING;
	interrupts_set(sig_enable);
	return tid;
}

/**************************************************************************
 * Important: The rest of the code should be implemented in Assignment 2. *
 **************************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	bool enabled = interrupts_off();
	struct wait_queue *wq;
	wq = malloc369(sizeof(struct wait_queue));
	assert(wq);
	wq->waitHead = NULL;
	wq->waitTail = NULL;
	interrupts_set(enabled);
	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	bool sig_enable = interrupts_off();
	if (wq != NULL) {
		free369(wq);
	}
	interrupts_set(sig_enable);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	bool enabled = interrupts_off();
	if (queue == NULL)
	{
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	if (readyHead == NULL && readyTail == NULL)
	{
		interrupts_set(enabled);
		return THREAD_NONE;
	}
	thread_pool[thread_id()]->state = SLEEP;
	enqueue_wait(thread_id(), queue);

	interrupts_set(enabled);
	return thread_yield(THREAD_ANY);
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	bool enabled = interrupts_off();
	if (queue == NULL || (queue->waitHead == NULL && queue->waitTail == NULL))
	{
		interrupts_set(enabled);
		return 0;
	}
	// if wake up all the thread(s) in the queue.
	int num_woken = 0;
	if (all)
	{
		while (queue->waitHead != NULL)
		{
			Tid tid = dequeue_wait(queue);
			thread_pool[tid]->state = READY;
			enqueue(tid);
			num_woken ++;
		}
	}
	else
	{
		Tid tid = dequeue_wait(queue);
		thread_pool[tid]->state = READY;
		enqueue(tid);
		num_woken ++;
	}
	interrupts_set(enabled);
	return num_woken;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid, int *exit_code)
{
	bool enabled = interrupts_off();
	if (tid < 0 || tid >= THREAD_MAX_THREADS || tid == cur_tid || thread_pool[tid] == NULL)
	{
		if (exit_code) *exit_code = THREAD_INVALID;
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	int if_first;
	if (thread_pool[tid]->wq == NULL)
	{
		thread_pool[tid]->wq = wait_queue_create();
		if_first = tid;
	}
	else if_first = THREAD_INVALID;
	thread_sleep(thread_pool[tid]->wq);

	if (exit_code)
	{
		*exit_code = exit_arr[tid];
		exit_arr[tid] = THREAD_INVALID;
	}
	interrupts_set(enabled);
	return if_first;
}

struct lock {
	/* ... Fill this in ... */
	Tid acquired;
	struct wait_queue* wq;
};

struct lock *
lock_create()
{
	int enabled = interrupts_off();
	struct lock *lock;

	lock = malloc369(sizeof(struct lock));
	assert(lock);

	lock->acquired = -1;
	lock->wq = wait_queue_create();

	interrupts_set(enabled);
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	bool enabled = interrupts_off();	

	assert(lock != NULL);
	wait_queue_destroy(lock->wq);
	free369(lock);

	interrupts_set(enabled);
}

void
lock_acquire(struct lock *lock)
{
	int enabled = interrupts_off();
	assert(lock != NULL);

	while (lock->acquired != -1)
	{
		thread_sleep(lock->wq);
	}

	lock->acquired = cur_tid;
	interrupts_set(enabled);
}

void
lock_release(struct lock *lock)
{
	int enabled = interrupts_off();
	assert(lock != NULL);

	if (lock->acquired == cur_tid)
	{
		lock->acquired = -1;
		thread_wakeup(lock->wq, 1);
	}
	interrupts_set(enabled);
}

struct cv {
	/* ... Fill this in ... */
	struct wait_queue* wq;
};

struct cv *
cv_create()
{
	int enabled = interrupts_off();
	struct cv *cv;

	cv = malloc369(sizeof(struct cv));
	assert(cv);

	cv->wq = wait_queue_create();
	interrupts_set(enabled);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	int enabled = interrupts_off();
	assert(cv != NULL);

	wait_queue_destroy(cv->wq);
	free369(cv);
	interrupts_set(enabled);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	int enabled = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);

	lock_release(lock);
	thread_sleep(cv->wq);
	lock_acquire(lock);
	interrupts_set(enabled);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	int enabled = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);

	if (lock->acquired == cur_tid) thread_wakeup(cv->wq, 0);
	interrupts_set(enabled);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	int enabled = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);

	if(lock->acquired == cur_tid) thread_wakeup(cv->wq, 1);
	interrupts_set(enabled);
}
