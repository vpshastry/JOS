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

	int64_t r, f, i;
	int rfd,n;
	struct Fd *fd;
	struct Fd fdcopy;
	struct Stat st;
	char buf[512];

	if ((rfd = open("/.journal", O_RDONLY)) < 0)
		panic("open /newmotd: %e", rfd);

	cprintf("Printing journal\n");
	while ((n = read(rfd, buf, sizeof buf-1)) > 0)
		sys_cputs(buf, n);
	cprintf("End of journal\n");

	close (rfd);

/*
	// We open files manually first, to avoid the FD layer
	if ((r = xopen("/newmotd", O_RDONLY)) < 0)
		panic("serve_open /newmotd: %e", r);
	if (FVA->fd_dev_id != 'f' || FVA->fd_offset != 0 || FVA->fd_omode != O_RDONLY)
		panic("serve_open did not fill struct Fd correctly\n");
	cprintf("serve_open is good\n");


	memset(buf, 0, sizeof buf);
	if ((r = devfile.dev_read(FVA, buf, sizeof buf)) < 0)
		panic("file_read: %e", r);
	cprintf("MSG: %s", buf);
	cprintf("file_read is good\n");
*/
}
