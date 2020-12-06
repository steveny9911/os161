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
#include <vm.h>
#include <addrspace.h>
#include <vfs.h>
#include <uio.h>
#include <vnode.h>
#include <stat.h>
#include <kern/fcntl.h>

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

// using a fixed size stack page for now
#define VM_STACKPAGES    18

static bool BOOT = false;
static int NUM_PAGES;

/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

/**
 * Coremap entry flags
 */
#define FREE 0    // free to use (unallocated)
#define FIXED 1   // fixed for coremap use; unable to use forever
#define CLEAN 2   // clean for swapping
#define DIRTY 3   // dirty when in-use (allocated)

/**
 * Global coremap array
 * Lock for coremap
 */
struct cm_entry *coremap;
static struct spinlock cm_spinlock = SPINLOCK_INITIALIZER;

#define CM_PID   0x3
#define CM_VADDR  0xfffff
#define CM_PADDR   0xfffff

void
vm_bootstrap(void)
{
	paddr_t lastaddr = ram_getsize();                           // get last physical address of free memory
	paddr_t firstaddr = ram_getfirstfree();                     // get first physical address of free memory

	NUM_PAGES = (lastaddr - firstaddr) / PAGE_SIZE;             // calculate NUM_PAGES (we are given PAGE_SIZE)

	coremap = (struct cm_entry*)PADDR_TO_KVADDR(firstaddr);     // allocate space to store coremap

	/*
	 *      | FIXED     | FREE             |
	 *      ^           ^                  ^
	 *  firstaddr    freeaddr            lastaddr
	 */

	// get size of coremap we made --- subtract that from actual virtual memory --- coremap should not be mapped as available memory
	paddr_t freeaddr = firstaddr + ROUNDUP(NUM_PAGES * sizeof(struct cm_entry), PAGE_SIZE);

	DEBUG(DB_EXEC, "firstaddr: %x\t freeaddr: %x\t lastaddr: %x\n", firstaddr, freeaddr, lastaddr);
	DEBUG(DB_EXEC, "NUM_PAGES: %d\n\n", NUM_PAGES);

	// initialize each coremap entry
	for (int i = 0; i < NUM_PAGES; i++) {
		coremap[i].cm_pid = curproc->p_pid;
		coremap[i].cm_vaddr = 0x0;
		coremap[i].cm_paddr = freeaddr + (unsigned long) i * PAGE_SIZE;    // consistent, evenly spaced, physical page address 

		// set pages used by coremap as FIXED
		// set other pages to FREE as unallocated
		//
		if (i < (int)(freeaddr - firstaddr) / PAGE_SIZE) {
			coremap[i].cm_flag = FIXED;
		} else {
			coremap[i].cm_flag = FREE;
		}
		coremap[i].cm_npages = 0;
	}

	// flag to indicate that vm is ready
	BOOT = true;
}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	// when vm has yet been bootstrapped
	// allocate physical memory for kernel
	//
	if (!BOOT) {
		spinlock_acquire(&stealmem_lock);
		addr = ram_stealmem(npages);
		spinlock_release(&stealmem_lock);
		return addr;
	}

	int firstpage = -1;
	int nfree = 0;

	spinlock_acquire(&cm_spinlock);

	// loop through the coremap
	// while finding the first FREE entry
	// count "nfree" so there are "npages" of contiguous FREE entry
	//
	for (int i = 0; i < NUM_PAGES && nfree != (int)npages; i++) {
		if (coremap[i].cm_flag == FREE) {
			if (firstpage == -1) {
				firstpage = i;
				addr = coremap[i].cm_paddr;
			}
			nfree++;
		} else {
			firstpage = -1;
			nfree = 0;
		}
	}

	if (firstpage == -1) {
		DEBUG(DB_EXEC, "no enough free pages\n");
		spinlock_release(&cm_spinlock);
		return 0;
	}

	// set these entries to DIRTY (allocated)
	for (int j = firstpage; j < firstpage + (int)npages; j++) {
		coremap[j].cm_flag = DIRTY;
		coremap[j].cm_npages = (int)npages;
	}

	DEBUG(DB_EXEC, "address of first page: %x\n", addr);
	spinlock_release(&cm_spinlock);
	return addr;
}

// Allocate/free some kernel-space virtual pages
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
	
	// loop through the coremap entry
	// find the matching entry with the same physical address
	// set the following contiguous entries to FREE
	//
	for (int i = 0; i < NUM_PAGES; i++) {
		if (coremap[i].cm_paddr == KVADDR_TO_PADDR(addr)) {
			npages = coremap[i].cm_npages;
			for (int j = i; j < i + npages; j++) {
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
	vaddr_t codebase, codetop, database, datatop, heapbase, heaptop, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_EXEC, "fault: 0x%x\n", faultaddress);
	DEBUG(DB_EXEC, "faulttype: %d\n", faulttype);

	switch (faulttype) {
		case VM_FAULT_READONLY:    // text segment should be read-only
			return EFAULT;
		case VM_FAULT_READ:
		case VM_FAULT_WRITE:
			break;
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
	KASSERT(as->as_vcodebase != 0);
	KASSERT(as->as_pcodebase != NULL);
	KASSERT(as->as_codepages != 0);
	
	KASSERT(as->as_vdatabase != 0);
	KASSERT(as->as_pdatabase != NULL);
	KASSERT(as->as_datapages != 0);

	KASSERT(as->as_heapbase != 0);
	KASSERT(as->as_heaptop != 0);
	
	KASSERT(as->as_stackbase != NULL);
	
	KASSERT((as->as_vcodebase & PAGE_FRAME) == as->as_vcodebase);
	
	KASSERT((as->as_vdatabase & PAGE_FRAME) == as->as_vdatabase);

	codebase = as->as_vcodebase;
	codetop = codebase + as->as_codepages * PAGE_SIZE;

	database = as->as_vdatabase;
	datatop = database + as->as_datapages * PAGE_SIZE;

	heapbase = as->as_heapbase;
	heaptop = as->as_heaptop;

	stackbase = USERSTACK - VM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	bool codesegment = false;               // indicate whether it is text-segment
	bool elf_loaded = as->elf_loaded;       // has ELF loaded yet

	// since we know page size
	// and they are evenly spaced
	// we can find the page number and look up in our coremap
	//
	if (faultaddress >= codebase && faultaddress < codetop) {
		int pcodepage = (faultaddress - codebase) / PAGE_SIZE;
		paddr = as->as_pcodebase[pcodepage];
		codesegment = true;
	}
	else if (faultaddress >= database && faultaddress < datatop) {
		int pdatapage = (faultaddress - database) / PAGE_SIZE;
		paddr = as->as_pdatabase[pdatapage];
	}
	else if (faultaddress >= heapbase && faultaddress < heaptop) {
		
		// for heap segment
		// locate the fault address inside the coremap
		// if found, return the physical address
		// else, allocate a new physical address
		//
		bool found = false;
		for (int i = 0; i < NUM_PAGES; i++) {
			if ((coremap[i].cm_vaddr == faultaddress)) {
				paddr = coremap[i].cm_paddr;
				found = true;
				break;
			}
		}
		if (!found) {
			paddr = getppages(1);
			for (int i = 0; i < NUM_PAGES; i++) {
				if (coremap[i].cm_paddr == paddr) {
					coremap[i].cm_vaddr = faultaddress;
				}
			}
		}
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		int stackpage = (faultaddress - stackbase) / PAGE_SIZE;
		paddr = as->as_stackbase[stackpage];
	}
	else {
		return EFAULT;
	}

	// make sure it's page-aligned
	KASSERT((paddr & PAGE_FRAME) == paddr);

	// Disable interrupts on this CPU while frobbing the TLB
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

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		kprintf("as_create kmalloc failed\n");
		return NULL;
	}

	as->as_vcodebase = 0;
	as->as_pcodebase = NULL;
	as->as_codepages = 0;

	as->as_vdatabase = 0;
	as->as_pdatabase = NULL;
	as->as_datapages = 0;
	
	as->as_stackbase = NULL;

	as->as_heapbase = 0;
	as->as_heaptop = 0;

	as->elf_loaded = false;

	return as;
}

void
as_destroy(struct addrspace *as)
{
	// simple free the memory for each page table array
	// covert from phsical address to virtual address
	// for freeing on coremap
	//
	for (size_t i = 0; i < as->as_codepages; i++) {
		free_kpages(PADDR_TO_KVADDR(as->as_pcodebase[i]));
	}

	for (size_t i = 0; i < as->as_datapages; i++) {
		free_kpages(PADDR_TO_KVADDR(as->as_pdatabase[i]));
	}

	for (size_t i = 0; i < VM_STACKPAGES; i++) {
		free_kpages(PADDR_TO_KVADDR(as->as_stackbase[i]));
	}

	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	// Disable interrupts on this CPU while frobbing the TLB
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	// nothing
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages;

	// Align the region. First, the base
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	// ...and now the length
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	(void)readable;
	(void)writeable;
	(void)executable;

	// define two regions called by ELF
	// initialize with virtual address for text and data segment
	// number of pages needed for each segment
	// page table array for each segment
	//
	if (as->as_vcodebase == 0) {
		DEBUG(DB_VM, "vcodebase: %x, codepages: %x\n", vaddr, npages);
		as->as_vcodebase = vaddr;
		as->as_codepages = npages;
		as->as_pcodebase = kmalloc(sizeof(paddr_t) * npages);
		return 0;
	}

	if (as->as_vdatabase == 0) {
		DEBUG(DB_EXEC, "vdatabase: %x, datapages: %x\n", vaddr, npages);
		as->as_vdatabase = vaddr;
		as->as_datapages = npages;
		as->as_pdatabase = kmalloc(sizeof(paddr_t) * npages);
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("Warning: too many regions\n");
	return ENOSYS;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	// super simple page table array that has
	// index as page number
	// value as physical address
	//
	// setup text, data, stack page table array
	// assign a physical address
	// then zero that address region
	//
	for (size_t i = 0; i < as->as_codepages; i++) {
		as->as_pcodebase[i] = getppages(1);
		as_zero_region(as->as_pcodebase[i], 1);
	}

	for (size_t i = 0; i < as->as_datapages; i++) {
		as->as_pdatabase[i] = getppages(1);
		as_zero_region(as->as_pcodebase[i], 1);
	}

	// region after text and data segment is for heap
	// stack is fixed size of VM_STACKPAGES for now
	// so we can mark the in-between region for heap
	//
	as->as_heapbase = as->as_vdatabase + (as->as_datapages + 1) * PAGE_SIZE;
	as->as_heaptop = as->as_heapbase;
	for (int i = 0; i < NUM_PAGES; i++) {
		if (coremap[i].cm_flag == FREE) {
			coremap[i].cm_vaddr = as->as_heapbase;
			break;
		}
	}

	as->as_stackbase = kmalloc(sizeof(paddr_t) * VM_STACKPAGES);
	if (as->as_stackbase == NULL) {
		return ENOMEM;
	}
	for (size_t i = 0; i < VM_STACKPAGES; i++) {
		as->as_stackbase[i] = getppages(1);
		as_zero_region(as->as_stackbase[i], 1);
	}

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->as_stackbase != 0);

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vcodebase = old->as_vcodebase;
	new->as_codepages = old->as_codepages;

	new->as_vdatabase = old->as_vdatabase;
	new->as_datapages = old->as_datapages;

	new->as_heapbase = old->as_heapbase;
	new->as_heaptop = old->as_heaptop;

	new->as_pcodebase = kmalloc(sizeof(paddr_t) * old->as_codepages);
	new->as_pdatabase = kmalloc(sizeof(paddr_t) * old->as_datapages);
	new->as_stackbase = kmalloc(sizeof(paddr_t) * VM_STACKPAGES);

	// (Mis)use as_prepare_load to allocate some physical memory
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	KASSERT(new->as_pcodebase != NULL);
	KASSERT(new->as_pdatabase != NULL);
	KASSERT(new->as_stackbase != NULL);

	// copy each page into the new area in memory
	// each page has a fixed size of PAGE_SIZE
	//
	for (size_t i = 0; i < new->as_codepages; i++) {
		memmove((void *)PADDR_TO_KVADDR(new->as_pcodebase[i]),
				(const void *)PADDR_TO_KVADDR(old->as_pcodebase[i]),
				PAGE_SIZE);
	}

	for (size_t i = 0; i < new->as_datapages; i++) {
		memmove((void *)PADDR_TO_KVADDR(new->as_pdatabase[i]),
				(const void *)PADDR_TO_KVADDR(old->as_pdatabase[i]),
				PAGE_SIZE);
	}

	for (size_t i = 0; i < VM_STACKPAGES; i++) {
		memmove((void *)PADDR_TO_KVADDR(new->as_stackbase[i]),
				(const void *)PADDR_TO_KVADDR(old->as_stackbase[i]),
				PAGE_SIZE);
	}

	*ret = new;
	return 0;
}