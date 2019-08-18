/*
 * Copyright (c) 2001, 2002, 2009
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
 * Driver code is in kern/tests/synchprobs.c We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

struct semaphore *male_sem;
struct semaphore *female_sem;
struct semaphore *match_sem;
volatile int male_waiting;
volatile int female_waiting;
volatile int match_waiting;
struct lock *whalemating_lock;
/*
 * Called by the driver during initialization.
 */

void whalemating_init() {
	male_sem = sem_create("male_sem", 0);
	female_sem = sem_create("female_sem", 0);
	match_sem = sem_create("match_sem", 0);
	whalemating_lock = lock_create("whalemating_lock");
	//TODO: panic if any of these aren't created
	male_waiting = 0;
	female_waiting = 0;
	match_waiting = 0;
	return;
}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {
	sem_destroy(male_sem);
	sem_destroy(female_sem);
	sem_destroy(match_sem);
	lock_destroy(whalemating_lock);
	return;
}

void
male(uint32_t index)
{
	/*
	 * Implement this function by calling male_start and male_end when
	 * appropriate.
	 */
	male_start(index);
	lock_acquire(whalemating_lock);
	if (female_waiting > 0 && match_waiting > 0) {
		V(female_sem);
		V(match_sem);
		lock_release(whalemating_lock);
		male_end(index);
	} else {
		male_waiting++;
		lock_release(whalemating_lock);
		P(male_sem);
		lock_acquire(whalemating_lock);
		male_waiting--;
		lock_release(whalemating_lock);
		male_end(index);
	}
	return;
}

void
female(uint32_t index)
{
	/*
	 * Implement this function by calling female_start and female_end when
	 * appropriate.
	 */
	female_start(index);
	lock_acquire(whalemating_lock);
	if (male_waiting > 0 && match_waiting > 0) {
		V(male_sem);
		V(match_sem);
		lock_release(whalemating_lock);
		female_end(index);
	} else {
		female_waiting++;
		lock_release(whalemating_lock);
		P(female_sem);
		lock_acquire(whalemating_lock);
		female_waiting--;
		lock_release(whalemating_lock);
		female_end(index);
	}
	return;
}

void
matchmaker(uint32_t index)
{
	/*
	 * Implement this function by calling matchmaker_start and matchmaker_end
	 * when appropriate.
	 */
	matchmaker_start(index);
	lock_acquire(whalemating_lock);
	if (female_waiting > 0 && male_waiting > 0) {
		V(female_sem);
		V(male_sem);
		lock_release(whalemating_lock);
		matchmaker_end(index);
	} else {
		match_waiting++;
		lock_release(whalemating_lock);
		P(match_sem);
		lock_acquire(whalemating_lock);
		match_waiting--;
		lock_release(whalemating_lock);
		matchmaker_end(index);
	}
	return;
}
