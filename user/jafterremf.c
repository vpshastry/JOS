#include <inc/lib.h>

#define FVA ((struct Fd*)0xCCCCC000)

static int
xopen(const char *path, int mode)
{
	extern union Fsipc fsipcbuf;
	envid_t fsenv;
	
	strcpy(fsipcbuf.open.req_path, path);
	fsipcbuf.open.req_omode = mode;

	fsenv = ipc_find_env(ENV_TYPE_FS);
	ipc_send(fsenv, FSREQ_OPEN, &fsipcbuf, PTE_P | PTE_W | PTE_U);
	return ipc_recv(NULL, FVA, NULL);
}
void
umain(int argc, char **argv)
{

	int rfd;
	char buf[512];
	int n, r;

	cprintf ("\n\nTrying to open the removed file\n");
	cprintf ("It fails as the transaction 'new-file' remove is recovered after restart\n");
	if ((rfd = open("/new-file", O_RDONLY)) < 0)
		panic("open /new-file: %e", rfd);
	cprintf ("Present: hehe\n");
	close(rfd);

}
