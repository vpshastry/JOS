#include <inc/lib.h>

const char *msg = "Does this persists?";

void
umain(int argc, char **argv)
{
	int64_t r, f, i;
	struct Fd *fd;
	struct Fd fdcopy;
	struct Stat st;
	char buf[512];
	int rfd,n;






	if ((rfd = open("/.journal", O_RDONLY)) < 0)
		panic("open /journal: %e", rfd);

	cprintf("Printing journal\n");
	while ((n = read(rfd, buf, sizeof buf-1)) > 0)
		sys_cputs(buf, n);
	cprintf("\n\nEnd of journal\n\n");
	
	close (rfd);

	

	
	return;
}
