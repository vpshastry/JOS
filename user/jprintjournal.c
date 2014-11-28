#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int rfd;
	char buf[512];
	int n, r;

	if ((rfd = open("/.journal", O_RDONLY)) < 0)
		panic("open /newmotd: %e", rfd);


	cprintf("Printing journal\n");
	while ((n = read(rfd, buf, sizeof buf-1)) > 0)
		sys_cputs(buf, n);
	cprintf("End of journal\n");

	close (rfd);
	return;
}
