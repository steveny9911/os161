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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <addrspace.h>

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 *
 * NOTE: it's been found over the years that students often begin on
 * the VM assignment by copying dumbvm.c and trying to improve it.
 * This is not recommended. dumbvm is (more or less intentionally) not
 * a good design reference. The first recommendation would be: do not
 * look at dumbvm at all. The second recommendation would be: if you
 * do, be sure to review it from the perspective of comparing it to
 * what a VM system is supposed to do, and understanding what corners
 * it's cutting (there are many) and why, and more importantly, how.
 */

/* under dumbvm, always have 72k of user stack */
/* (this must be > 64K so argument blocks of size ARG_MAX will fit) */
#define DUMBVM_STACKPAGES    18

static bool BOOT = false;
static int NUM_PAGES;

/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

/**
 * Core map
 */
#define FREE 0
#define FIXED 1
#define CLEAN 2
#define DIRTY 3

struct cm_entry *coremap;
static struct spinlock cm_spinlock = SPINLOCK_INITIALIZER;

void
vm_bootstrap(void)
{
	// get last physical address of free memory
	paddr_t lastaddr = ram_getsize();

	// get first physical address of free memory
	paddr_t firstaddr = ram_getfirstfree();
	
	// calculate NUM_PAGES (we are given PAGE_SIZE)
	NUM_PAGES = (lastaddr - firstaddr) / PAGE_SIZE;

	// allocate space to store coremap (but! coremap should not be mapped as available memory)
	coremap = (struct cm_entry*)PADDR_TO_KVADDR(firstaddr);

	// get size of coremap we made --- subtract that from actual virtual memory --- coremap should not be mapped as available memory
	paddr_t freeaddr = firstaddr + ROUNDUP(NUM_PAGES * sizeof(struct cm_entry), PAGE_SIZE);

	kprintf("firstaddr: %x\t freeaddr: %x\t lastaddr: %x\n", firstaddr, freeaddr, lastaddr);
	kprintf("NUM_PAGES: %d\n\n", NUM_PAGES);

	//     | FIXED     | FREE             |
	//     ^           ^                  ^
	// firstaddr    freeaddr            lastaddr

	// initialize each coremap entry
	for (int i = 0; i < NUM_PAGES; i++) {
		coremap[i].cm_addr = freeaddr + (unsigned long) i * PAGE_SIZE;
		coremap[i].cm_npages = 0;

		if (i < (int)(freeaddr - firstaddr) / PAGE_SIZE) {
			coremap[i].cm_flag = FIXED;
		} else {
			coremap[i].cm_flag = FREE;
		}
	}

	// need a flag to indicate that vm has already bootstrapped
	BOOT = true;
}

static
paddr_t
getppages(unsigned long npages)
{
	// kprintf("=== enter getppages npages: %lu ===\n", npages);
	paddr_t addr;
	if (!BOOT) {
		spinlock_acquire(&stealmem_lock);
		addr = ram_stealmem(npages);
		spinlock_release(&stealmem_lock);
		return addr;
	}

	int firstpage = -1;
	int nfree = 0;

	spinlock_acquire(&cm_spinlock);
	for (int i = 0; i < NUM_PAGES && nfree != (int)npages; i++) {
		if (coremap[i].cm_flag == FREE) {
			if (firstpage == -1) {
				firstpage = i;
				addr = coremap[i].cm_addr;
			}
			nfree++;
		} else {
			firstpage = -1;
			nfree = 0;
		}
	}

	if (firstpage == -1) {
		spinlock_release(&cm_spinlock);
		return 0;
	}

	for (int j = firstpage; j < firstpage + (int)npages; j++) {
		coremap[j].cm_flag = DIRTY;
		coremap[j].cm_npages = (int)npages;
	}

	spinlock_release(&cm_spinlock);
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t addr;
	
	addr = getppages(npages);
	if (addr == 0) {
		return 0;
	}
	return PADDR_TO_KVADDR(addr);
}

void
free_kpages(vaddr_t addr)
{
	int npages;

	spinlock_acquire(&cm_spinlock);
	for (int i = 0; i < NUM_PAGES; i++) {
		if (coremap[i].cm_addr == KVADDR_TO_PADDR(addr)) {
			npages = coremap[i].cm_npages;

			for (int j = i; j < npages; j++) {
				coremap[j].cm_flag = FREE;
				coremap[j].cm_npages = 0;
			}
		}
	}

	spinlock_release(&cm_spinlock);
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
			// /* We always create pages read-write, so we can't get this */
			// panic("dumbvm: got VM_FAULT_READONLY\n");
			return EFAULT;
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
	    default:
			return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	bool codesegment = false;
	bool elf_loaded = as->elf_loaded;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
		codesegment = true;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		if (codesegment && elf_loaded) {
			elo &= ~TLBLO_DIRTY;
		}
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	ehi = faultaddress;
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	if (codesegment && elf_loaded) {
		elo &= ~TLBLO_DIRTY;
	}
	DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
	tlb_random(ehi, elo);
	splx(spl);
	return 0;
}
