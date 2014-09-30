/*See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>
#include <kern/macro.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushq $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushq $0;							\
	pushq $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
   Find the PUSHA,POPA macros in kern/macro.h. Since amd64 doesn't support
   pusha,popa so you will find these macros handy.
 */

TRAPHANDLER_NOEC(traphandler0, T_DIVIDE)
TRAPHANDLER_NOEC(traphandler1, T_DEBUG)
TRAPHANDLER_NOEC(traphandler2, T_NMI)
TRAPHANDLER_NOEC(traphandler3, T_BRKPT)
TRAPHANDLER_NOEC(traphandler4, T_OFLOW)
TRAPHANDLER_NOEC(traphandler5, T_BOUND)
TRAPHANDLER_NOEC(traphandler6, T_ILLOP)
TRAPHANDLER_NOEC(traphandler7, T_DEVICE)
TRAPHANDLER(traphandler8, T_DBLFLT)
TRAPHANDLER_NOEC(traphandler9, 9)
TRAPHANDLER(traphandler10, T_TSS)
TRAPHANDLER(traphandler11, T_SEGNP)
TRAPHANDLER(traphandler12, T_STACK)
TRAPHANDLER(traphandler13, T_GPFLT)
TRAPHANDLER(traphandler14, T_PGFLT)
TRAPHANDLER_NOEC(syscallhandler48, T_SYSCALL)

/*
 * Lab 3: Your code here for _alltraps
 *
 * Hint: Be sure to review the x64 calling convention from lab1
 *       for how to pass a pointer to the trapframe.
 */

_alltraps:
	subq $16, %rsp;
	movw %ds, 8(%rsp);
	movw %es, 0(%rsp);
	PUSHA;
	movq $GD_KD, %rax;
	movq %rax, %ds;
	movq %rax, %es;
	movq %rsp, %rdi;
	call trap;
	POPA_;
	iret;
