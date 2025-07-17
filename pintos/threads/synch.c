/* This file is derived from source code for the Nachos
	 instructional operating system.  The Nachos copyright notice
	 is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
	 All rights reserved.

	 Permission to use, copy, modify, and distribute this software
	 and its documentation for any purpose, without fee, and
	 without written agreement is hereby granted, provided that the
	 above copyright notice and the following two paragraphs appear
	 in all copies of this software.

	 IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
	 ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
	 CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
	 AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
	 HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

	 THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
	 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
	 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
	 PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
	 BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
	 PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
	 MODIFICATIONS.
	 */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static bool
compare_sema_priority_desc(const struct list_elem *a,
													 const struct list_elem *b,
													 void *aux UNUSED);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
	 nonnegative integer along with two atomic operators for
	 manipulating it:

	 - down or "P": wait for the value to become positive, then
	 decrement it.

	 - up or "V": increment the value (and wake up one waiting
	 thread, if any). */
void sema_init(struct semaphore *sema, unsigned value)
{
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
	 to become positive and then atomically decrements it.

	 This function may sleep, so it must not be called within an
	 interrupt handler.  This function may be called with
	 interrupts disabled, but if it sleeps then the next scheduled
	 thread will probably turn interrupts back on. This is
	 sema_down function. */
void sema_down(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();
	while (sema->value == 0)
	{
		list_insert_ordered(&sema->waiters, &thread_current()->elem, compare_priority_desc, NULL);
		thread_block();
	}
	sema->value--;
	intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
	 semaphore is not already 0.  Returns true if the semaphore is
	 decremented, false otherwise.

	 This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
	 and wakes up one thread of those waiting for SEMA, if any.

	 This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);

	struct thread *sema_thread;

	if (!list_empty(&sema->waiters))
	{
		old_level = intr_disable();
		sema->value++;
		sema_thread = list_entry(list_pop_front(&sema->waiters), struct thread, elem);
		intr_set_level(old_level);

		thread_unblock(sema_thread);

		if (thread_current()->priority < sema_thread->priority)
			thread_yield();
	}
	else
	{
		old_level = intr_disable();
		sema->value++;
		intr_set_level(old_level);
	}
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
	 between a pair of threads.  Insert calls to printf() to see
	 what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
	 thread at any given time.  Our locks are not "recursive", that
	 is, it is an error for the thread currently holding a lock to
	 try to acquire that lock.

	 A lock is a specialization of a semaphore with an initial
	 value of 1.  The difference between a lock and such a
	 semaphore is twofold.  First, a semaphore can have a value
	 greater than 1, but a lock can only be owned by a single
	 thread at a time.  Second, a semaphore does not have an owner,
	 meaning that one thread can "down" the semaphore and then
	 another one "up" it, but with a lock the same thread must both
	 acquire and release it.  When these restrictions prove
	 onerous, it's a good sign that a semaphore should be used,
	 instead of a lock. */
/* Lock을 초기화한다. 락은 한 번에 최대 하나의 스레드만 소유할 수 있다.
	 이 락은 재귀적(recursion)이 아니므로, 이미 락을 소유한 스레드가
	 다시 동일한 락을 획득하려고 하면 오류가 발생한다.

	 이 락은 초기값이 1인 세마포어의 특수한 형태이다.
	 락과 일반 세마포어의 차이점은 두 가지이다.
	 첫째, 세마포어는 값이 1보다 클 수 있지만, 락은 항상 하나의 스레드만 소유할 수 있다.
	 둘째, 세마포어에는 소유자 개념이 없어서 한 스레드가 down 연산을 하고
	 다른 스레드가 up 연산을 수행해도 되지만,
	 라은 반드시 같은 스레드가 획득(acquire)하고 해제(release)해야 한다.
	 이 제약이 부담스럽다면, 락 대신 세마포어를 사용하는 것이 좋다.*/
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
	 necessary.  The lock must not already be held by the current
	 thread.

	 This function may sleep, so it must not be called within an
	 interrupt handler.  This function may be called with
	 interrupts disabled, but interrupts will be turned back on if
	 we need to sleep. */
/* 락(LOCK)을 획득한다. 필요하다면 사용 가능해질 때까지 대기(sleep)한다.
	 현재 스레드가 이미 이 락을 소유하고 있어서는 안 된다.

	 이 함수는 대기 상태로 들어갈 수 있으므로, 인터럽트 핸들러 내에서 호출하면 안 된다.
	 또한 인터럽트가 비활성화된 상태에서도 호출할 수 있지만,
	 대기할 때는 인터럽트를 다시 활성화한다. */
void lock_acquire(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	sema_down(&lock->semaphore);
	lock->holder = thread_current();
}

/* Tries to acquires LOCK and returns true if successful or false
	 on failure.  The lock must not already be held by the current
	 thread.

	 This function will not sleep, so it may be called within an
	 interrupt handler. */
/* LOCK을 획득 시도하고, 성공하면 true를, 실패하면 false를 반환한다.
	 현재 스레드가 이미 이 락을 소유하고 있어서는 안 된다.

	 이 함수는 대기 상태(sleep)를 발생시키지 않으므로, 인터럽트 핸들러 내에서도 호출할 수 있다.*/
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
	 This is lock_release function.

	 An interrupt handler cannot acquire a lock, so it does not
	 make sense to try to release a lock within an interrupt
	 handler. */
/* LOCK을 해제한다. 이 락은 반드시 현재 스레드가 소유하고 있어야 한다.
	 이것이 Lock_release 함수이다.

	 인터럽트 핸들러는 락을 획득할 수 없으므로,
	 인터럽트 핸들러 내에서 락을 해제하는 것은 의미가 없다. */
void lock_release(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	lock->holder = NULL;
	sema_up(&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
	 otherwise.  (Note that testing whether some other thread holds
	 a lock would be racy.) */
/* 현재 스레드가 LOCK을 소유하고 있으면 true를, 그렇지 않으면 false를 반환한다.
	 (다른 스레드가 락을 소유하고 있는지 검사하는 것은 경쟁(race)이 발생할 수 있다.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
	struct list_elem elem;			/* List element. */
	struct semaphore semaphore; /* This semaphore. */
	int priority;								/* 대기 시작 시점의 스레드 우선순위 */
};

/* Initializes condition variable COND.  A condition variable
	 allows one piece of code to signal a condition and cooperating
	 code to receive the signal and act upon it. */
/* 조건 변수 COND를 초기화한다.
	 조건 변수는 한쪽 코드가 어떤 조건을 신호(signaling)하면,
	 협력하는 다른 코드가 그 신호를 받아 처리할 수 있도록 해 준다. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
	 some other piece of code.  After COND is signaled, LOCK is
	 reacquired before returning.  LOCK must be held before calling
	 this function.

	 The monitor implemented by this function is "Mesa" style, not
	 "Hoare" style, that is, sending and receiving a signal are not
	 an atomic operation.  Thus, typically the caller must recheck
	 the condition after the wait completes and, if necessary, wait
	 again.

	 A given condition variable is associated with only a single
	 lock, but one lock may be associated with any number of
	 condition variables.  That is, there is a one-to-many mapping
	 from locks to condition variables.

	 This function may sleep, so it must not be called within an
	 interrupt handler.  This function may be called with
	 interrupts disabled, but interrupts will be turned back on if
	 we need to sleep. */
/* LOCK을 원자적으로 해제한 뒤, COND가 다른 코드에 의해 신호(signaled)될 때까지 대기한다.
	 COND에 신호가 발생하면, 반환하기 전에 LOCK을 다시 획득한다.
	 이 함수를 호출하기 전에 LOCK이 이미 획득되어 있어야 한다.

	 이 함수가 구현하는 모니터는 "Mesa" 스타일이며, "Hoare" 스타일이 아니다.
	 즉, 신호를 보내는 것과 받는 것은 원자적인 연산이 아니므로,
	 일반적으로 대기가 끝난 후에는 조건을 다시 검사하고,
	 필요하다면 다시 대기해야 한다.

	 하나의 조건 변수는 단 하나의 락과 연결되지만,
	 하나의 락은 여러 개의 조건 변수와 연결될 수 있다.
	 즉, 락 하나에 조건 변수가 여러 개 매핑될 수 있다.

	 이 함수는 잠들 수 있으므로, 인터럽트 핸들러 내부에서 호출해서는 안 된다.
	 또한, 인터럽트가 비활성화된 상태에서 호출할 수 있으나,
	 대기 상태로 들어가면 인터럽는 다시 활성화된다.*/
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	sema_init(&waiter.semaphore, 0);
	waiter.priority = thread_current()->priority;
	list_insert_ordered(&cond->waiters, &waiter.elem, compare_sema_priority_desc, NULL);
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
	 this function signals one of them to wake up from its wait.
	 LOCK must be held before calling this function.

	 An interrupt handler cannot acquire a lock, so it does not
	 make sense to try to signal a condition variable within an
	 interrupt handler. */
/* 만약 LOCK으로 보호된 COND에 대기 중인 스레드가 있다면,
	 그중 하나에게 신호를 보내 대기에서 깨어나도록 한다.
	 이 함수를 호출하기 전에 LOCK이 반드시 획득되어 있어야 한다.

	 인터럽트 핸들러는 락을 획득할 수 없으므로,
	 인터럽트 핸들러 내에서 조건 변수에 신호를 보내는 것은 의미가 없다. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters))
	{
		sema_up(&list_entry(list_pop_front(&cond->waiters), struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
	 LOCK).  LOCK must be held before calling this function.

	 An interrupt handler cannot acquire a lock, so it does not
	 make sense to try to signal a condition variable within an
	 interrupt handler. */
/* LOCK으로 보호된 COND에 대기 중인 모든 스레드를 깨운다.
	 이 함수를 호출하기 전에 LOCK이 반드시 획득되어 있어야 한다.

	 인터럽트 핸들러는 락을 획득할 수 없으므로,
	 인터럽트 핸들러 내부에서 조건 변수에 신호를 보내는 것은 의미가 없다. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}

static bool
compare_sema_priority_desc(const struct list_elem *a,
													 const struct list_elem *b,
													 void *aux UNUSED)
{
	struct semaphore_elem *sema_a = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sema_b = list_entry(b, struct semaphore_elem, elem);

	return sema_a->priority > sema_b->priority;
}