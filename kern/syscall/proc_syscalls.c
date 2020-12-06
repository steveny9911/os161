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
 * Process-related syscalls.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/wait.h>
#include <lib.h>
#include <machine/trapframe.h>
#include <clock.h>
#include <thread.h>
#include <proc.h>
#include <current.h>
#include <copyinout.h>
#include <pid.h>
#include <syscall.h>
#include <addrspace.h>

#define VM_STACKPAGES    18

/* note that sys_execv is in runprogram.c */


/*
 * sys_getpid
 * love easy syscalls. :)
 */
int
sys_getpid(pid_t *retval)
{
	*retval = curproc->p_pid;
	return 0;
}

/*
 * sys__exit()
 *
 * The process-level work (exit status, waking up waiters, etc.)
 * happens in proc_exit(). Then call thread_exit() to make our thread
 * go away too.
 */
__DEAD
void
sys__exit(int status)
{
	proc_exit(_MKWAIT_EXIT(status));
	thread_exit();
}

/*
 * sys_fork
 *
 * create a new process, which begins executing in fork_newthread().
 */

static
void
fork_newthread(void *vtf, unsigned long junk)
{
	struct trapframe mytf;
	struct trapframe *ntf = vtf;

	(void)junk;

	/*
	 * Now copy the trapframe to our stack, so we can free the one
	 * that was malloced and use the one on our stack for going to
	 * userspace.
	 */

	mytf = *ntf;
	kfree(ntf);

	enter_forked_process(&mytf);
}

int
sys_fork(struct trapframe *tf, pid_t *retval)
{
	struct trapframe *ntf;
	int result;
	struct proc *newproc;

	/*
	 * Copy the trapframe to the heap, because we might return to
	 * userlevel and make another syscall (changing the trapframe)
	 * before the child runs. The child will free the copy.
	 */

	ntf = kmalloc(sizeof(struct trapframe));
	if (ntf==NULL) {
		return ENOMEM;
	}
	*ntf = *tf;

	result = proc_fork(&newproc);
	if (result) {
		kfree(ntf);
		return result;
	}
	*retval = newproc->p_pid;

	result = thread_fork(curthread->t_name, newproc,
			     fork_newthread, ntf, 0);
	if (result) {
		proc_unfork(newproc);
		kfree(ntf);
		return result;
	}

	return 0;
}

/*
 * sys_waitpid
 * just pass off the work to the pid code.
 */
int
sys_waitpid(pid_t pid, userptr_t retstatus, int flags, pid_t *retval)
{
	int status;
	int result;

	result = pid_wait(pid, &status, flags, retval);
	if (result) {
		return result;
	}

	if (retstatus != NULL) {
		result = copyout(&status, retstatus, sizeof(int));
	}
	return result;
}

int
sys_sbrk(intptr_t amount, int* retval) 
{
	struct addrspace *as = proc_getas();
	vaddr_t heapbreak = as->as_heaptop;
	
	// check for alignment
	if (amount % PAGE_SIZE != 0) {
		DEBUG(DB_EXEC, "amount is not aligned\n");
		*retval = (int)((void *)-1);
		return EINVAL;
	}

	// if sbrk(0), return the heaptop
	if (amount == 0) {
		*retval = (int)heapbreak;
		return 0;
	}

	// when amount less than zero
	// if sum less than heapbase, then negative memory access which is not allow
	// else, decrease heaptop
	//
	if (amount < 0) {
		if (heapbreak + amount < as->as_heapbase) {
			DEBUG(DB_EXEC, "heaptop hits heapbase\n");
			*retval = (int)((void *)-1);
			return EINVAL;
		} else {
			as->as_heaptop += amount;
			*retval = (int)heapbreak;
			return 0;
		}
	} 

	// final check if added sum exceeds to stack segment, not allowed
	// else increase the heaptop, return original new heaptop
	//
	if (as->as_heaptop + amount >= PADDR_TO_KVADDR(as->as_stackbase[VM_STACKPAGES-1])) {
		DEBUG(DB_EXEC, "heaptop hits stacktop\n");
		*retval = (int)((void *)-1);
		return ENOMEM;
	}

	as->as_heaptop += amount;
	*retval = (int)heapbreak;
	return 0;
}