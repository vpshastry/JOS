#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int rfd;
	char buf[512];
	int n, r;

	cprintf ("\n\nTrying to open the removed file\n");
	cprintf ("It fails as the transaction 'new-file' remove is recovered after restart\n");
	if ((rfd = open("/big", O_RDONLY)) < 0)
		panic("open /new-file: %e", rfd);
	cprintf ("Present: hehe\n");
	close(rfd);
}
