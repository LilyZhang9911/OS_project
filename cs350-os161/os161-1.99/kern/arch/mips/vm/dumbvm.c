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
#include "opt-A3.h"

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground.
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

#if OPT_A3
int coremap_entries, coremap_pages; 
int *coremap; 
paddr_t coremap_start, coremap_end;
bool coremap_setup = false; 
#endif /* OPT_A3 */

void
vm_bootstrap(void)
{
#if OPT_A3
	ram_getsize(&coremap_start, &coremap_end); // both are physical addresses
	// page align start and end
	coremap_start = ROUNDUP(coremap_start, PAGE_SIZE); 
	coremap_end = ROUNDUP(coremap_end, PAGE_SIZE) - PAGE_SIZE; 
	coremap_entries = (coremap_end - coremap_start) /  PAGE_SIZE; // compute number of entries
	coremap = (int *)PADDR_TO_KVADDR(coremap_start);
        
	// compute the size of coremap and how many pages it will take up
	coremap_pages = ROUNDUP(sizeof(int) * coremap_entries, PAGE_SIZE) / PAGE_SIZE;
	for (int i = 0; i < coremap_entries; ++i) {
		if(i < coremap_pages) {
			coremap[i] = i+1; // allocate the first coremap_pages for the coremap itself
		} else {
			coremap[i] = 0;
		}
	}
	coremap_setup = true;
#endif
}

#if OPT_A3
// returns physical address of allocated page
static paddr_t
coremap_getppages (int npages) {
	int pages_start = 0;
	bool found = false;
	for (pages_start = coremap_pages; pages_start < coremap_entries; ++pages_start){	
		for (int j = 0; j < npages; ++j) {
			if (coremap[pages_start + j] != 0) {
				break; 
			} else if (j == npages - 1 && coremap[pages_start + j] == 0) {
				found = true; 
				break; 
			}

		}
		// mark the frames
		if (found) {
			for (int j = 0; j < npages; ++j) {
				coremap[pages_start + j] = j+1; 
			}
			break; 
		} 
	}
	
 /*	kprintf("marking completed\n"); 
	for (int i = 0; i < coremap_entries; ++i) {
		kprintf("i: %d %d\n", i, coremap[i]); 
	}
	kprintf ("\n\n\n");  */
	// assuming allocation is always successful 
	return coremap_start + (pages_start * PAGE_SIZE); 
}
#endif

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);
#if OPT_A3
	//kprintf ("getting %d pages\n", (int)npages); 
	if (coremap_setup) {
		addr = coremap_getppages((int)npages); 
	} else {
		addr = ram_stealmem(npages);
	}
#else
	addr = ram_stealmem(npages); 
#endif
	spinlock_release(&stealmem_lock);
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr)
{
#if OPT_A3
	spinlock_acquire(&stealmem_lock); 
	//kprintf ("free called with vaddr %p\n", (int *) addr); 
	paddr_t paddr = addr - MIPS_KSEG0; // convert to physical address	
	int index = (paddr - coremap_start) / PAGE_SIZE; 
	KASSERT (index >= coremap_pages && index <= coremap_entries && coremap[index] != 0);
        
	int max_index = index + 1, min_index = index; 
	// find start of segment
	while (coremap[min_index] != 1) {
		min_index--; 
	}
	// find the end of the segment
	int prev_val = coremap[index]; 
	while (coremap[max_index] == prev_val + 1) {
		prev_val = coremap[max_index]; 
		max_index++; 
	}
	// mark the pages in the range as free
	for (int i = min_index; i < max_index; ++i) {
		coremap[i] = 0;
	}
	//kprintf ("freed %d pages\n", max_index - min_index); 
	/*kprintf ("AFTER FREE\n"); 
	for (int i = 0; i < 100; ++i) {
		kprintf("i: %d %d\n", i, coremap[i]); 
	}*/
	spinlock_release(&stealmem_lock); 
	
#else
	/* nothing - leak the memory. */
	(void)addr;
#endif /* OPT_A3 */
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
	//kprintf ("vm_fault on paddr  %p\n", (int *)faultaddress); 
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;
#if OPT_A3
	bool read_only = false; 
#endif

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);
	switch (faulttype) {
	    case VM_FAULT_READONLY:
#if OPT_A3
		   //kprintf ("Read only!\n"); 
	  	return EFAULT; // not sure if this is the right message
#else
    		    /* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
#endif
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

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}
	/* Assert that the address space has been set up properly. */
#if OPT_A3
	//int index = 0;
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != NULL);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != NULL);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != NULL);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	int index = 0; 
#else
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);
#endif
	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;
	


	if (faultaddress >= vbase1 && faultaddress < vtop1) {
#if OPT_A3	
		index = (faultaddress - vbase1) / PAGE_SIZE; 
		//kprintf ("code index %d\n", index); 
		paddr = as->as_pbase1[index];
		read_only = true; 	
		//kprintf ("%p\n", (int *) paddr); 
#else
		paddr = (faultaddress - vbase1) + as->as_pbase1;

#endif
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
#if OPT_A3
		index = (faultaddress - vbase2) / PAGE_SIZE;
		//kprintf ("data index %d\n", index); 	
		paddr = as->as_pbase2[index];
		//kprintf ("%p\n", (int *) paddr); 
#else
		paddr = (faultaddress - vbase2) + as->as_pbase2;
#endif
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
#if OPT_A3
		index = (faultaddress - stackbase) / PAGE_SIZE;
		//kprintf ("stack index %d\n", index); 
		paddr = as->as_stackpbase[index];
		//kprintf ("%p\n", (int *) paddr); 
#else
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
#endif
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
#if OPT_A3
		if (read_only && as->load_complete) {
			//kprintf ("loaded read only %p\n", (int *) paddr); 
			elo &= ~TLBLO_DIRTY; 
		}
#endif
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}
#if OPT_A3
	ehi = faultaddress;
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	tlb_random(ehi, elo); 
	splx(spl);
	return 0; 

#else
	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
#endif
}


struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}
#if OPT_A3
	as->as_vbase1 = 0;
	as->as_pbase1 = NULL;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = NULL;
	as->as_npages2 = 0;
	as->as_stackpbase = NULL;
	as->load_complete = false; 

#else
	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
#endif
	return as;
}

void
as_destroy(struct addrspace *as)
{
#if OPT_A3
	for (int i = 0; i < (int) as->as_npages1; ++i) {
		free_kpages(PADDR_TO_KVADDR (as->as_pbase1[i]));
	}
	kfree (as->as_pbase1); 
	
	for (int i = 0; i < (int) as->as_npages2; ++i) {
		free_kpages(PADDR_TO_KVADDR (as->as_pbase2[i]));
	}
	kfree (as->as_pbase2);

	for (int i = 0; i < (int) DUMBVM_STACKPAGES; ++i) {
		free_kpages(PADDR_TO_KVADDR (as->as_stackpbase[i]));
	}
	kfree (as->as_stackpbase); 
#endif
	kfree(as);
}

void
as_activate(void)
{
	//kprintf ("as activate called\n"); 
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;
#if OPT_A3
	as->as_stackpbase = kmalloc (DUMBVM_STACKPAGES * sizeof (paddr_t));
#endif	
	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
#if OPT_A3
		as->as_pbase1 = kmalloc (npages * sizeof (paddr_t)); 
#endif
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
#if OPT_A3
		as->as_pbase2 = kmalloc (npages * sizeof (paddr_t));
#endif

		return 0;
	}


	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

#if OPT_A3
static
void
as_zero_region(paddr_t* paddr, unsigned npages)
{
	for (int i = 0; i < (int) npages; ++i) {
		bzero((void *)PADDR_TO_KVADDR(paddr[i]), 1);
	}
}

#else
static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}
#endif


int
as_prepare_load(struct addrspace *as)
{
#if OPT_A3
	for (int i = 0; i < (int) as->as_npages1; ++i) {
		as->as_pbase1[i] = getppages(1);
		//kprintf ("pbase1 loaded %p %d/%d\n", (int *)as->as_pbase1[i], i+1, as->as_npages1); 
		if ((int *) as->as_pbase1[i] == NULL) return ENOMEM;
	}
	
	for (int i = 0; i < (int) as->as_npages2; ++i) {
		as->as_pbase2[i] = getppages(1);
		if ((int *) as->as_pbase2[i] == NULL) return ENOMEM;
		//kprintf ("pbase2 loaded %p %d/%d\n", (int *) as->as_pbase2[i], i+1, as->as_npages2); 
	}

	for (int i = 0; i < (int) DUMBVM_STACKPAGES; ++i) {
		as->as_stackpbase[i] = getppages(1);
		//kprintf ("stack loaded %p %d/%d\n", (int *)as->as_stackpbase[i], i+1, DUMBVM_STACKPAGES); 
		if ((int *) as->as_stackpbase[i] == NULL) return ENOMEM;
	}	

	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);
#else
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}
	
	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);
#endif
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
	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	//kprintf ("AS COPY CALLED!\n"); 
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

#if OPT_A3
	new->as_pbase1 = kmalloc (old->as_npages1 * sizeof (paddr_t)); 
	new->as_pbase2 = kmalloc (old->as_npages2 * sizeof (paddr_t)); 
	new->as_stackpbase = kmalloc (DUMBVM_STACKPAGES * sizeof (paddr_t)); 
#endif

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}
#if OPT_A3
	for (int i = 0; i < (int) new->as_npages1; ++i) {
		memmove((void *)PADDR_TO_KVADDR(new->as_pbase1[i]),
			(const void *)PADDR_TO_KVADDR(old->as_pbase1[i]),
			PAGE_SIZE);
	}
	for (int i = 0; i < (int) new->as_npages2; ++i) {
		memmove((void *)PADDR_TO_KVADDR(new->as_pbase2[i]),
			(const void *)PADDR_TO_KVADDR(old->as_pbase2[i]),
			PAGE_SIZE);
	}
	for (int i = 0; i < (int) DUMBVM_STACKPAGES; ++i) {
		memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase[i]),
			(const void *)PADDR_TO_KVADDR(old->as_stackpbase[i]),
			PAGE_SIZE);
	}
#
#else
	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);
#endif	
	*ret = new;
	return 0;
}
