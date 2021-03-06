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
#include <copyinout.h>
#include <synch.h>
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

static paddr_t coremap_start;
static paddr_t coremap_end;
static uint64_t coremap_pages;
static int* core_array;
static bool core_map_available = false;

void
vm_bootstrap(void)
{
	paddr_t coremap;
	/* Create coremap and set flag to be true. */
	ram_getsize(&coremap, &coremap_end);
	coremap_pages = (coremap_end - coremap)/PAGE_SIZE;
	uint64_t pg_in_use = (sizeof(int)*coremap_pages/PAGE_SIZE + 1);
	coremap_start = coremap + PAGE_SIZE*pg_in_use;
	coremap_pages -= pg_in_use;
	core_map_available = true;
	
	// initialize coremap in virtual address.
	core_array = (int*)PADDR_TO_KVADDR(coremap);
	for (unsigned i = 0; i < coremap_pages; i++) {
		core_array[i] = 0;
	}
}
#else
vm_bootstrap(void)
{
	/* Do nothing. */
}
#endif

#if OPT_A3
static unsigned long getfreeblocksize(int* start) {
	unsigned long offset = 0;
	while (start[offset] == 0) {
		offset++;
	}
	return offset;
}


static paddr_t  coremap_stealmem(unsigned long npages) {
	unsigned i;
	for (i = 0; i<coremap_pages-npages+1; i++) {
		if (core_array[i] == 0) {
			unsigned long free_pages = getfreeblocksize(core_array+i);
			if (free_pages >= npages) {
				for (unsigned j = 1; j<=npages; j++) {
					core_array[i+j-1] = j;
					//kprintf("core_array[%d]: %d\n", i+j-1, j);
				}
				return coremap_start+i*PAGE_SIZE;
			}
			i+=free_pages-1;
		}
	}
	return 0;
}
#endif

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);
	#if OPT_A3
	if (core_map_available) {
		addr = coremap_stealmem(npages);
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
	//kprintf("alloc address: %x\n", pa);
	return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr)
{
	//kprintf("free_kpages address: %p\n", (void*)addr);
	#if OPT_A3
	spinlock_acquire(&stealmem_lock);
	KASSERT(addr != 0);
	paddr_t paddr = KVADDR_TO_PADDR(addr);
	bool is_first_block = true;
	unsigned offset = (paddr - coremap_start)/PAGE_SIZE;
	//core_array = PADDR_TO_KVADDR(coremap);
	int i = 0;
	while (is_first_block == true || (core_array[offset] > 1 && offset<coremap_pages)) {
		is_first_block = false;
		core_array[offset] = 0;
		offset += 1;
		i++;
	}
	spinlock_release(&stealmem_lock);
	#endif
	(void)addr;
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
		/* We always create pages read-write, so we can't get this */
		#if OPT_A3
  		return EROFS;
  		#else
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

#if OPT_A3
	bool is_text = false;
#endif

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
		#if OPT_A3
		is_text = true;
		#endif
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
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		#if OPT_A3
		if (is_text && as->is_loaded) {
			elo &= ~TLBLO_DIRTY;
		}
		#endif
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

#if OPT_A3
	ehi = faultaddress;
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	DEBUG(DB_VM, "dumbvm_random_eviction: 0x%x -> 0x%x\n", faultaddress, paddr);
	#if OPT_A3
	if (is_text && as->is_loaded) {
		elo &= ~TLBLO_DIRTY;
	}
	#endif
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
  	as->is_loaded = false;
  	#endif
	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;

	return as;
}

void
as_destroy(struct addrspace *as)
{
	free_kpages(PADDR_TO_KVADDR(as->as_pbase1));
	free_kpages(PADDR_TO_KVADDR(as->as_pbase2));
	free_kpages(PADDR_TO_KVADDR(as->as_stackpbase));
	kfree(as);
}

void
as_activate(void)
{
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

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
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

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	#if OPT_A3
	as_activate();
	as->is_loaded = true;
	#else
	(void)as;
	#endif
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr, char** args, unsigned argc)
{
	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK - 128;

	uint32_t args_start = USERSTACK - 128 + 4*(argc+1); // 1 for NULL after args pointers
	uint32_t null_bytes = 0;
	int fatal = copyout((void*)&null_bytes, (userptr_t)args_start-4, 4);
	if (fatal) {
		return fatal;
	}
	for (unsigned i = 0; i<argc; i++) {
		unsigned actual;
		copyoutstr(args[i], (userptr_t)args_start, 128, &actual);
		copyout((void*)(&args_start), (userptr_t)(*stackptr+i*4), 4);

		args_start+=actual;
	}

	return 0;
}

int
as_build_stack(struct addrspace *as, vaddr_t *stackptr, char* args, size_t argc)
{
	(void)*args;
	(void)argc;
	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;

	size_t actual = 0;
	uint32_t args_start = USERSTACK - 128 + 4*(argc+1); // 1 for NULL after args pointers
	uint32_t null_bytes = 0;
	int fatal = copyout((void*)&null_bytes, (userptr_t)args_start-4, 4);
	if (fatal) {
		return fatal;
	}
	size_t total_len = 0;
	for (size_t i = 0; i<argc; i++) {
		fatal = copyout((void*)(&args_start), (userptr_t)USERSTACK-128+(i)*4, 4);
		if (fatal) {
			return fatal;
		}
		fatal = copyoutstr(args+total_len, (userptr_t)args_start, 128, &actual);
		if (fatal) {
			return fatal;
		}
	// kprintf("-----------------------out---\n");
  	// // for (size_t i = total_len; i<total_len+actual; i++) {
    // // 	if (args[i] == '\0') {
    // //   		kprintf("\\0\n");
    // //   	} else 
    // //   		kprintf("%c", args[i]);
  	// // }
	//   kprintf("args_start[%d]: %d", i, args_start);
	//  // kprintf("argv[%d]: %d", i, USERSTACK-128+(i+1)*sizeof(void*));
   	// kprintf("------------------------out---\n");
		args_start+=actual;
		total_len+=actual;
	}

	// uint32_t start;
	// copyin((userptr_t)*a1, &start, 8);
	// kprintf("start: %d", start);
	// copyin((userptr_t)*a1+4, &start, 8);
	// kprintf("start: %d", start);
	// copyin((userptr_t)*a1+8, &start, 8);
	// kprintf("start: %d", start);
	// lock_release(curproc->p_mutex);
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

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

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
	
	*ret = new;
	return 0;
}
