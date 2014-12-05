// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/dwarf.h>
#include <kern/kdebug.h>
#include <kern/dwarf_api.h>
#include <kern/trap.h>

#include <kern/sched.h>
#include <kern/env.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display the backtrace of the function", mon_backtrace },
	{ "restartFS", "Restart the FS", mon_restartFS},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_restartFS(int argc, char **argv, struct Trapframe *tf)
{
	cprintf ("Restarting FS\n");
	ENV_CREATE(fs_fs, ENV_TYPE_FS);
	cprintf ("Restarting FS done.\n");
	ENV_CREATE(user_writemotd, ENV_TYPE_USER);
	sched_yield();
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint64_t		rbp_reg = 0x00;
	uint64_t		rip_reg = 0x00;
	uint64_t		*rbp_ptr= 0x00;
	struct Ripdebuginfo	info	= {0,};
	int			i	= 0;

	/* Read the initial RIP and RBP */
	rbp_reg = read_rbp();
	read_rip (rip_reg);
	cprintf("\nStack backtrace:\n");

	while (rbp_reg) {
		rbp_ptr = (uint64_t *)rbp_reg;

		/* Print rbp and rip */
		cprintf("  rbp %016x  rip %016x\n", rbp_reg, rip_reg);

		debuginfo_rip(rip_reg, &info);

		/* Print filename:line_number functionname:current_exec_addr
		   no_of_args */
		cprintf("	%s:%d: ", info.rip_file, info.rip_line);
		cprintf("%.*s", info.rip_fn_namelen, info.rip_fn_name);
		cprintf("+%016x  args:%d ", rip_reg - info.rip_fn_addr,
			info.rip_fn_narg);

		/* Print the actual arguments */
		for (i = 1; i <= info.rip_fn_narg; i++)
			cprintf (" %016x", *(((int *) rbp_ptr) - i));
		cprintf ("\n");

		/* Follow the RBP */
		rbp_reg = *rbp_ptr;
		rip_reg = *(rbp_ptr + 1);
	}

	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
