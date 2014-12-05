/*
 * File system server main loop -
 * serves IPC requests from other environments.
 */

#include <inc/x86.h>
#include <inc/string.h>

#include "fs.h"


#define debug 0

// The file system server maintains three structures
// for each open file.
//
// 1. The on-disk 'struct File' is mapped into the part of memory
//    that maps the disk.  This memory is kept private to the file
//    server.
// 2. Each open file has a 'struct Fd' as well, which sort of
//    corresponds to a Unix file descriptor.  This 'struct Fd' is kept
//    on *its own page* in memory, and it is shared with any
//    environments that have the file open.
// 3. 'struct OpenFile' links these other two structures, and is kept
//    private to the file server.  The server maintains an array of
//    all open files, indexed by "file ID".  (There can be at most
//    MAXOPEN files open concurrently.)  The client uses file IDs to
//    communicate with the server.  File IDs are a lot like
//    environment IDs in the kernel.  Use openfile_lookup to translate
//    file IDs to struct OpenFile.


// Max number of open files in the file system at once
#define MAXOPEN		1024
#define FILEVA		0xD0000000

struct OpenFile {
	uint32_t o_fileid;	// file id
	struct File *o_file;	// mapped descriptor for open file
	int o_mode;		// open mode
	struct Fd *o_fd;	// Fd page
};

// initialize to force into data section
struct OpenFile opentab[MAXOPEN] = {
	{ 0, 0, 1, 0 }
};

// Virtual address at which to receive page mappings containing client requests.
union Fsipc *fsreq = (union Fsipc *)0x0ffff000;

int serve_write (envid_t envid, union Fsipc *ipc);
int serve_trunc (envid_t envid, union Fsipc *ipc);

void
serve_init(void)
{
	int i;
	uintptr_t va = FILEVA;
	for (i = 0; i < MAXOPEN; i++) {
		opentab[i].o_fileid = i;
		opentab[i].o_fd = (struct Fd*) va;
		va += PGSIZE;
	}
}

// Allocate an open file.
int
openfile_alloc(struct OpenFile **o)
{
	int i, r;

	// Find an available open-file table entry
	for (i = 0; i < MAXOPEN; i++) {
		switch (pageref(opentab[i].o_fd)) {
		case 0:
			if ((r = sys_page_alloc(0, opentab[i].o_fd, PTE_P|PTE_U|PTE_W)) < 0)
				return r;
			/* fall through */
		case 1:
			opentab[i].o_fileid += MAXOPEN;
			*o = &opentab[i];
			memset(opentab[i].o_fd, 0, PGSIZE);
			return (*o)->o_fileid;
		}
	}
	return -E_MAX_OPEN;
}

// Look up an open file for envid.
int
openfile_lookup(envid_t envid, uint32_t fileid, struct OpenFile **po)
{
	struct OpenFile *o;

	o = &opentab[fileid % MAXOPEN];
	if (pageref(o->o_fd) == 1 || o->o_fileid != fileid)
		return -E_INVAL;
	*po = o;
	return 0;
}

// Open req->req_path in mode req->req_omode, storing the Fd page and
// permissions to return to the calling environment in *pg_store and
// *perm_store respectively.
int
serve_open(envid_t envid, struct Fsreq_open *req,
	   void **pg_store, int *perm_store)
{
	char path[MAXPATHLEN];
	struct File *f;
	int fileid;
	int r;
	struct OpenFile *o;

	if (debug)
		cprintf("serve_open %08x %s 0x%x\n", envid, req->req_path, req->req_omode);

	// Copy in the path, making sure it's null-terminated
	memmove(path, req->req_path, MAXPATHLEN);
	path[MAXPATHLEN-1] = 0;

	if (strcmp (path, ".journal") == 0) {
		cprintf ("hehe?\n");
		cprintf ("%.*s", jfile->f_size, jfile->f_direct[0]);
		return 0;
	}

	// Find an open file ID
	if ((r = openfile_alloc(&o)) < 0) {
		//if (debug)
			cprintf("openfile_alloc failed: %e", r);
		return r;
	}
	fileid = r;

	/*if (req->req_omode != 0) {
		//if (debug)
			cprintf("file_open omode 0x%x unsupported", req->req_omode);
		return -E_INVAL;
	}
	*/

	if ((r = file_open(path, &f)) < 0) {
		// Error out if not (create mode and enotfound)
		if (!((req->req_omode & O_CREAT) && r == -E_NOT_FOUND)) {
			if (debug)
				cprintf("file_open failed: %e", r);
			return r;
		}

		if (debug)
			cprintf ("Creating new file: %s\n", path);

		r = handle_ocreate (path, &f);
		if (r < 0) {
			cprintf ("\nnew creation failed: %e\n", r);
			return r;
		}
	}

	if (req->req_omode & O_TRUNC) {
		if (debug)
			cprintf ("Truncating the file: %s\n", f->f_name);

		handle_otrunc (f, 0);
	}

	// Save the file pointer
	o->o_file = f;

	// Fill out the Fd structure
	o->o_fd->fd_file.id = o->o_fileid;
	o->o_fd->fd_omode = req->req_omode & O_ACCMODE;
	o->o_fd->fd_dev_id = devfile.dev_id;
	o->o_mode = req->req_omode;

	if (debug)
		cprintf("sending success, page %08x\n", (uintptr_t) o->o_fd);

	// Share the FD page with the caller by setting *pg_store,
	// store its permission in *perm_store
	*pg_store = o->o_fd;
	*perm_store = PTE_P|PTE_U|PTE_W|PTE_SHARE;

	cprintf ("path: %s, crashfilepath: %s\n", path, CRASHFILEPATH);
	if (! strcmp (path, CRASHFILEPATH)) {
		cprintf ("Setting the crash bit\n");
		crash_testing = 1;
	}

	return 0;
}


// Read at most ipc->read.req_n bytes from the current seek position
// in ipc->read.req_fileid.  Return the bytes read from the file to
// the caller in ipc->readRet, then update the seek position.  Returns
// the number of bytes successfully read, or < 0 on error.
int
serve_read(envid_t envid, union Fsipc *ipc)
{
	struct Fsreq_read *req = &ipc->read;
	struct Fsret_read *ret = &ipc->readRet;

	if (debug)
		cprintf("serve_read %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	// Look up the file id, read the bytes into 'ret', and update
	// the seek position.  Be careful if req->req_n > PGSIZE
	// (remember that read is always allowed to return fewer bytes
	// than requested).  Also, be careful because ipc is a union,
	// so filling in ret will overwrite req.
	//
	// LAB 5: Your code here
	//panic("serve_read not implemented");
	struct OpenFile	*openfile	= NULL;
	size_t		size_read	= -1;
	int		r		= 0;

	r = openfile_lookup (envid, req->req_fileid, &openfile);
	if (r < 0) {
		cprintf ("\n\n\n Failed to lookup @serve_read\n\n\n");
		return r;
	}

	if (req->req_n > PGSIZE)
		req->req_n = PGSIZE;

	size_read = file_read (openfile->o_file, ret->ret_buf, req->req_n,
				openfile->o_fd->fd_offset);
	if (size_read < 0) {
		cprintf ("\n\n\n failed to read file @serve_read\n\n\n");
		return size_read;
	}

	openfile->o_fd->fd_offset += size_read;

	return size_read;
}



// Stat ipc->stat.req_fileid.  Return the file's struct Stat to the
// caller in ipc->statRet.
int
serve_stat(envid_t envid, union Fsipc *ipc)
{
	struct Fsreq_stat *req = &ipc->stat;
	struct Fsret_stat *ret = &ipc->statRet;
	struct OpenFile *o;
	int r;

	if (debug)
		cprintf("serve_stat %08x %08x\n", envid, req->req_fileid);

	if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;

	strcpy(ret->ret_name, o->o_file->f_name);
	ret->ret_size = o->o_file->f_size;
	ret->ret_isdir = (o->o_file->f_type == FTYPE_DIR);
	return 0;
}


// Our read-only file system do nothing for flush
int
serve_flush(envid_t envid, struct Fsreq_flush *req)
{
	//struct Fsreq_flush *req = &ipc->flush;

	if (debug)
		cprintf("serve_flush %08x %08x\n", envid, req->req_fileid);

	struct OpenFile	*openfile	= NULL;
	int		r		= 0;

	r = openfile_lookup (envid, req->req_fileid, &openfile);
	if (r < 0) {
		cprintf ("\n\n\n Failed to lookup @serve_trunc\n\n\n");
		return r;
	}

	file_flush (openfile->o_file);

	return 0;
}

int
serve_remove(envid_t envid, union Fsipc *ipc)
{
	struct Fsreq_remove *req = &ipc->remove;
	char path[MAXPATHLEN];

	if (debug)
		cprintf("serve_remove %08x %08x\n", envid, req->req_path);

	strncpy (path, req->req_path, MAXPATHLEN);
	path[MAXPATHLEN -1] = '\0';

	return file_remove (path);
}

int
serve_sync(envid_t envid, union Fsipc *req)
{
	fs_sync ();
	return 0;
}

typedef int (*fshandler)(envid_t envid, union Fsipc *req);

fshandler handlers[] = {
	// Open is handled specially because it passes pages
	/* [FSREQ_OPEN] =	(fshandler)serve_open, */
	[FSREQ_READ] =		serve_read,
	[FSREQ_STAT] =		serve_stat,
	[FSREQ_FLUSH] =		(fshandler)serve_flush,

	// while making writeable FS
	[FSREQ_WRITE] = 	serve_write,
	[FSREQ_TRUNC] = 	serve_trunc,
	[FSREQ_SYNC] = 		serve_sync,
	[FSREQ_REMOVE] = 	serve_remove,
};
#define NHANDLERS (sizeof(handlers)/sizeof(handlers[0]))

void
serve(void)
{
	uint32_t req, whom;
	int perm, r;
	void *pg;

	while (1) {
		perm = 0;
		req = ipc_recv((int32_t *) &whom, fsreq, &perm);
		if (debug)
			cprintf("fs req %d from %08x [page %08x: %s]\n",
				req, whom, uvpt[PGNUM(fsreq)], fsreq);


		// All requests must contain an argument page
		if (!(perm & PTE_P)) {
			cprintf("Invalid request from %08x: no argument page\n",
				whom);
			continue; // just leave it hanging...
		}

		pg = NULL;
		if (req == FSREQ_OPEN) {
			r = serve_open(whom, (struct Fsreq_open*)fsreq, &pg, &perm);
		} else if (req < NHANDLERS && handlers[req]) {
			r = handlers[req](whom, fsreq);
		} else {
			cprintf("Invalid request code %d from %08x\n", req, whom);
			r = -E_INVAL;
		}

		ipc_send(whom, r, pg, perm);
		sys_page_unmap(0, fsreq);
	}
}

void
umain(int argc, char **argv)
{
	static_assert(sizeof(struct File) == 256);
	binaryname = "fs";
	cprintf("FS is running\n");

	// Check that we are able to do I/O
	outw(0x8A00, 0x8A00);
	cprintf("FS can do I/O\n");

	serve_init();
	fs_init();
	fs_test();
	serve();
}

// Below are created while implementing writeable FS
int
serve_write (envid_t envid, union Fsipc *ipc)
{
	struct Fsreq_write *req = &ipc->write;

	if (debug)
		cprintf("serve_write %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	struct OpenFile	*openfile	= NULL;
	size_t		size_written	= -1;
	int		r		= 0;

	r = openfile_lookup (envid, req->req_fileid, &openfile);
	if (r < 0) {
		cprintf ("\n\n\n Failed to lookup @serve_write\n\n\n");
		return r;
	}

	if (req->req_n > (PGSIZE - (sizeof (int) + sizeof (size_t))))
		req->req_n = PGSIZE - (sizeof (int) + sizeof (size_t));

	size_written = file_write (openfile->o_file, req->req_buf, req->req_n,
					openfile->o_fd->fd_offset);
	if (size_written < 0) {
		cprintf ("\n\n\n failed to write file @serve_write\n\n\n");
		return size_written;
	}

	openfile->o_fd->fd_offset += size_written;
	return size_written;
}

int
serve_trunc (envid_t envid, union Fsipc *ipc)
{
	struct Fsreq_trunc *req = &ipc->trunc;

	if (debug)
		cprintf("serve_trunc %08x %08x %08x\n", envid,
				req->req_fileid, req->req_n);

	struct OpenFile	*openfile	= NULL;
	int		r		= 0;

	r = openfile_lookup (envid, req->req_fileid, &openfile);
	if (r < 0) {
		cprintf ("\n\n\n Failed to lookup @serve_trunc\n\n\n");
		return r;
	}

	if (req->req_n % BLKSIZE) {
		cprintf ("We currently don't support non aligned trunc");
		return -1;
	}

	r = handle_otrunc (openfile->o_file, req->req_n);
	if (r < 0) {
		cprintf ("\n\n\n failed to trunc file @serve_trunc\n\n\n");
		return r;
	}

	openfile->o_fd->fd_offset = 0;
	openfile->o_file->f_size = 0;
	return 0;
}
