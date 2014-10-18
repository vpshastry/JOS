// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;
	int ret = 0;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write pag_e.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	// What is FEC_WAR dude?
	if (!(err & FEC_WR))
		panic ("Sorry you can't go beyond this with your error status");
	if (!(uvpt[PGNUM (addr)] & PTE_COW))
		panic ("Not a write or cow page");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
	ret = sys_page_alloc (0, PFTEMP, PTE_U | PTE_P | PTE_W);
	if (ret < 0)
		panic ("Page alloc failed: %e", ret);

	memcpy ((void *)PFTEMP, ROUNDDOWN (addr, PGSIZE), PGSIZE);

	// Check on the envids
	ret = sys_page_map (0, PFTEMP, 0, ROUNDDOWN (addr, PGSIZE),
				PTE_P | PTE_U | PTE_W);
	if (ret < 0)
		panic ("page map failed: %e", ret);

	ret = sys_page_unmap (0, PFTEMP);
	if (ret < 0)
		panic ("Failed unmap: %e", ret);
	//panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	int	ret	= 0;
	uint64_t	perm	= PTE_P | PTE_U;

	if ((uvpt[pn] & (uint64_t)PTE_W) || (uvpt[pn] & (int64_t)PTE_COW))
		perm |= (PTE_COW);

	// LAB 4: Your code here.
	//panic("duppage not implemented");
	// Make sure about the source and destination envids
	ret = sys_page_map (0, (void *)(uintptr_t)(pn * PGSIZE), envid,
				(void *)(uintptr_t)(pn * PGSIZE), perm);
	if (ret < 0)
		panic ("page mapping for child failed");

	// Set its own to page mapping to COW
	ret = sys_page_map (0, (void *)(uintptr_t)(pn * PGSIZE), 0,
				(void *)(uintptr_t)(pn * PGSIZE), perm);
	if (ret < 0)
		panic ("page mapping for itself is failed");

	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	//panic("fork not implemented");

	envid_t		childenvid	= 0;
	unsigned	pn		= 0;
	uint8_t		*i		= 0;
	int		ret		= 0;
	extern unsigned char end[];
	const volatile struct Env	*myenv		= NULL;

	set_pgfault_handler (pgfault);

	childenvid = sys_exofork ();
	if (childenvid < 0) {
		panic ("Failed syscall exofork");
		return childenvid;

	} else if (childenvid == 0) {
		// Child process
		thisenv = &envs[ENVX(sys_getenvid ())];
		return 0;

	}

	// Parent process
	myenv = &envs[ENVX(sys_getenvid ())];

	for (i = (uint8_t *)UTEXT; i < end; i += PGSIZE) {
		pn = PGNUM (i);

		if (!((uvpt [pn]) & PTE_P) ||
				i == (uint8_t *)(UXSTACKTOP - PGSIZE))
			continue;

		ret = duppage (childenvid, pn);
		if (ret < 0)
			panic ("Failed to duppage: %e", ret);
	}
	// Don't insert any instruction modifying the 'i' until before duppage
	// Copy the stack
	duppage (childenvid, PGNUM (ROUNDDOWN ((USTACKTOP - PGSIZE), PGSIZE)));

	ret = sys_page_alloc (childenvid, (void *)(UXSTACKTOP - PGSIZE),
				PTE_U | PTE_W | PTE_P);
	if (ret < 0) {
		cprintf ("Failed to allocate page uxstack");
		return ret;
	}

	ret = sys_env_set_pgfault_upcall (childenvid,
						myenv->env_pgfault_upcall);
	if (ret < 0) {
		cprintf ("Failed to set pgfault upcall handler");
		return ret;
	}

	ret = sys_env_set_status (childenvid, ENV_RUNNABLE);
	if (ret < 0) {
		cprintf ("Failed o set env status");
		return ret;
	}

	return childenvid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
