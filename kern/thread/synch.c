/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
	struct semaphore *sem;

	sem = kmalloc(sizeof(*sem));
	if (sem == NULL)
	{
		return NULL;
	}

	sem->sem_name = kstrdup(name);
	if (sem->sem_name == NULL)
	{
		kfree(sem);
		return NULL;
	}

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL)
	{
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
	sem->sem_count = initial_count;

	return sem;
}

void sem_destroy(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
	kfree(sem->sem_name);
	kfree(sem);
}

void P(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
	while (sem->sem_count == 0)
	{
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
	}
	KASSERT(sem->sem_count > 0);
	sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void V(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

	sem->sem_count++;
	KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(*lock));
	if (lock == NULL)
	{
		return NULL;
	}

	lock->lk_name = kstrdup(name);
	if (lock->lk_name == NULL)
	{
		kfree(lock);
		return NULL;
	}

	HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);

	lock->lk_wchan = wchan_create(lock->lk_name);
	if (lock->lk_wchan == NULL)
	{
		kfree(lock->lk_name);
		kfree(lock);
		return NULL;
	}

	spinlock_init(&lock->lk_spinlock);
	lock->lk_thread = NULL;
	return lock;
}

void lock_destroy(struct lock *lock)
{
	KASSERT(lock != NULL);
	KASSERT(lock->lk_thread == NULL);
	//KASSERT(wchan_isempty(lock->lk_wchan, &lock->lk_spinlock));  //TODO why does this not work
	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&lock->lk_spinlock);
	wchan_destroy(lock->lk_wchan);
	kfree(lock->lk_name);
	kfree(lock);
}

void lock_acquire(struct lock *lock)
{
	KASSERT(lock != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the lock_acquire without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);

	/* Use the lock spinlock to protect the wchan as well. */
	spinlock_acquire(&lock->lk_spinlock);
	/* Call this (atomically) before waiting for a lock */
	HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);
	while (lock->lk_thread != NULL)
	{
		wchan_sleep(lock->lk_wchan, &lock->lk_spinlock);
	}
	KASSERT(lock->lk_thread == NULL);
	lock->lk_thread = curthread;
	/* Call this (atomically) once the lock is acquired */
	HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);
	spinlock_release(&lock->lk_spinlock);
}

void lock_release(struct lock *lock)
{

	KASSERT(lock != NULL);

	spinlock_acquire(&lock->lk_spinlock);
	KASSERT(lock->lk_thread == curthread);
	lock->lk_thread = NULL;
	wchan_wakeone(lock->lk_wchan, &lock->lk_spinlock);
	/* Call this (atomically) when the lock is released */
	HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);
	spinlock_release(&lock->lk_spinlock);
}

bool lock_do_i_hold(struct lock *lock)
{
	KASSERT(lock != NULL);

	return lock->lk_thread == curthread;
}

////////////////////////////////////////////////////////////
//
// CV

struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(*cv));
	if (cv == NULL)
	{
		return NULL;
	}

	cv->cv_name = kstrdup(name);
	if (cv->cv_name == NULL)
	{
		kfree(cv);
		return NULL;
	}

	// add stuff here as needed
	cv->cv_wchan = wchan_create(cv->cv_name);
	if (cv->cv_wchan == NULL)
	{
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}
	spinlock_init(&cv->cv_spinlock);
	// ran
	return cv;
}
void cv_destroy(struct cv *cv)
{
	KASSERT(cv != NULL);

	// add stuff here as needed
	wchan_destroy(cv->cv_wchan);
	spinlock_cleanup(&cv->cv_spinlock);
	kfree(cv->cv_name);
	kfree(cv);
}

void cv_wait(struct cv *cv, struct lock *lock)
{
	// Write this
	KASSERT(lock_do_i_hold(lock));
	/* Release the supplied lock*/
	spinlock_acquire(&cv->cv_spinlock);
	lock_release(lock);
	/* Go to sleep */
	wchan_sleep(cv->cv_wchan, &cv->cv_spinlock);
	/* After wakeup reacquire the lock */
	spinlock_release(&cv->cv_spinlock);
	lock_acquire(lock);
}

void cv_signal(struct cv *cv, struct lock *lock)
{
	// Write this
	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&lock->lk_spinlock);
	wchan_wakeone(cv->cv_wchan, &lock->lk_spinlock);
	spinlock_release(&lock->lk_spinlock);
}

void cv_broadcast(struct cv *cv, struct lock *lock)
{
	// Write this
	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&lock->lk_spinlock);
	wchan_wakeall(cv->cv_wchan, &lock->lk_spinlock);
	spinlock_release(&lock->lk_spinlock);
}

// struct cv *rw_reader;
// struct cv *rw_writer;
// struct lock *lock;

struct rwlock *
rwlock_create(const char *name)
{
	struct rwlock *rwlock;
	rwlock = kmalloc(sizeof(*rwlock));
	if (rwlock == NULL)
	{
		return NULL;
	}

	rwlock->rwlock_name = kstrdup(name);
	if (rwlock->rwlock_name == NULL)
	{
		kfree(rwlock);
		return NULL;
	}

	rwlock->rw_reader = cv_create(name);
	if (rwlock->rw_reader == NULL)
	{
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}

	rwlock->rw_writer = cv_create(name);
	if (rwlock->rw_writer == NULL)
	{
		cv_destroy(rwlock->rw_reader);
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}

	rwlock->lock = lock_create(name);
	if (rwlock->lock == NULL)
	{
		cv_destroy(rwlock->rw_writer);
		cv_destroy(rwlock->rw_reader);
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}

	rwlock->reading = 0;
	rwlock->writing = false;
	rwlock->writer_waiting = 0;
	rwlock->reader_waiting = false;
	return rwlock;
}

void rwlock_destroy(struct rwlock *rwlock)
{
	KASSERT(rwlock != NULL);
	// add stuff here as needed
	cv_destroy(rwlock->rw_writer);
	cv_destroy(rwlock->rw_reader);
	lock_destroy(rwlock->lock);
	kfree(rwlock->rwlock_name);
	kfree(rwlock);
}

// states
// reading 		  - r
// writing 		  - w
// reader waiting - rw
// writer waiting - ww

// r & rw won't occur unless ww
// r & ww
// w & ww
// w & rw
void rwlock_acquire_read(struct rwlock *rwlock)
{
	lock_acquire(rwlock->lock);
	while (rwlock->writing || rwlock->writer_waiting > 0)
	{
		rwlock->reader_waiting = true;
		cv_wait(rwlock->rw_reader, rwlock->lock);
	}
	//rwlock->writing = false;
	rwlock->reading++;
	rwlock->reader_waiting = false;
	lock_release(rwlock->lock);
};

void rwlock_acquire_write(struct rwlock *rwlock)
{
	lock_acquire(rwlock->lock);
	while (rwlock->writing || rwlock->reading > 0)
	{
		rwlock->writer_waiting++;
		cv_wait(rwlock->rw_writer, rwlock->lock);
	}
	rwlock->writing = true;
	rwlock->writer_waiting--;
	lock_release(rwlock->lock);
}

void rwlock_release_read(struct rwlock *rwlock)
{
	lock_acquire(rwlock->lock);
	rwlock->reading--;
	if (rwlock->reading == 0 && rwlock->writer_waiting > 0)
	{
		cv_signal(rwlock->rw_writer, rwlock->lock);
	}
	lock_release(rwlock->lock);
}

void rwlock_release_write(struct rwlock *rwlock)
{
	lock_acquire(rwlock->lock);
	rwlock->writing = false;
	if (rwlock->reader_waiting)
	{
		cv_broadcast(rwlock->rw_reader, rwlock->lock);
	}
	else if (rwlock->writer_waiting > 0)
	{
		cv_signal(rwlock->rw_writer, rwlock->lock);
	}
	lock_release(rwlock->lock);
}