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
/*
#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int rfd, wfd;
	char buf[512];
	int n, r;

	if ((rfd = open("/newmotd", O_RDONLY)) < 0)
		panic("open /newmotd: %e", rfd);
	if ((wfd = open("/motd", O_RDWR)) < 0)
		panic("open /motd: %e", wfd);
	cprintf("file descriptors %d %d\n", rfd, wfd);
	if (rfd == wfd)
		panic("open /newmotd and /motd give same file descriptor");

	cprintf("OLD MOTD\n===\n");
	while ((n = read(wfd, buf, sizeof buf-1)) > 0)
		sys_cputs(buf, n);
	cprintf("===\n");
	seek(wfd, 0);

	if ((r = ftruncate(wfd, 0)) < 0)
		panic("truncate /motd: %e", r);

	cprintf("NEW MOTD\n===\n");
	while ((n = read(rfd, buf, sizeof buf-1)) > 0) {
		sys_cputs(buf, n);
		if ((r = write(wfd, buf, n)) != n)
			panic("write /motd: %e", r);
	}
	cprintf("===\n");

	if (n < 0)
		panic("read /newmotd: %e", n);

	close(rfd);
	close(wfd);

	if ((rfd = open("/.journal", O_RDONLY)) < 0)
		panic("open /newmotd: %e", rfd);

	cprintf("Printing journal\n");
	while ((n = read(rfd, buf, sizeof buf-1)) > 0)
		sys_cputs(buf, n);
	cprintf("End of journal\n");

	close (rfd);
	return;
}
*/
