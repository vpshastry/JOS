#include <inc/string.h>

#include "fs.h"

#define debug 0

// --------------------------------------------------------------
// Super block
// --------------------------------------------------------------

// Validate the file system super-block.
void
check_super(void)
{
	if (super->s_magic != FS_MAGIC)
		panic("bad file system magic number");

	if (super->s_nblocks > DISKSIZE/BLKSIZE)
		panic("file system is too large");

	cprintf("superblock is good\n");
}

// --------------------------------------------------------------
// File system structures
// --------------------------------------------------------------

// Initialize the file system
void
fs_init(void)
{
	static_assert(sizeof(struct File) == 256);

	// Find a JOS disk.  Use the second IDE disk (number 1) if available.
	if (ide_probe_disk1())
		ide_set_disk(1);
	else
		ide_set_disk(0);

	bc_init();

	// Set "super" to point to the super block.
	super = diskaddr(1);
	check_super();

	bitmap_init ();
	journal_init ();
	//journal_checkand_repair ();
}

// Find the disk block number slot for the 'filebno'th block in file 'f'.
// Set '*ppdiskbno' to point to that slot.
// The slot will be one of the f->f_direct[] entries,
// or an entry in the indirect block.
// When 'alloc' is set, this function will allocate an indirect block
// if necessary.
//
//  Note, for the read-only file system (lab 5 without the challenge),
//        alloc will always be false.
//
// Returns:
//	0 on success (but note that *ppdiskbno might equal 0).
//	-E_NOT_FOUND if the function needed to allocate an indirect block, but
//		alloc was 0.
//	-E_NO_DISK if there's no space on the disk for an indirect block.
//	-E_INVAL if filebno is out of range (it's >= NDIRECT + NINDIRECT).
//
// Analogy: This is like pgdir_walk for files.
// Hint: Don't forget to clear any block you allocate.
static int
file_block_walk(struct File *f, uint32_t filebno, uint32_t **ppdiskbno,
		bool alloc)
{
        // LAB 5: Your code here.
        //panic("file_block_walk not implemented");
	uint32_t	*ib_addr	= NULL;

	if (filebno >= (NDIRECT + NINDIRECT))
		return -E_INVAL;

	if (filebno < NDIRECT) {
		*ppdiskbno = &f->f_direct[filebno];
		goto out;
	}

	if (!f->f_indirect) {
		if (!alloc)
			return -E_NOT_FOUND;

		//panic ("Can't allocate in a read-only FS");
		// Allocate a block for indirect addressing
		f->f_indirect = alloc_block (f);
		memset ((void *)diskaddr ((uint64_t)f->f_indirect), 0x0,
				PGSIZE);
	}

	ib_addr = (uint32_t *) diskaddr ((uint64_t)f->f_indirect);

	*ppdiskbno = (uint32_t *)&(ib_addr[filebno - NDIRECT]);

	// If alloc flag set and the diskblock number is zero, allocate one
out:
	if (alloc && ((**ppdiskbno) == 0))
		**ppdiskbno = alloc_block (f);

	return 0;
}

// Set *blk to the address in memory where the filebno'th
// block of file 'f' would be mapped.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_NO_DISK if a block needed to be allocated but the disk is full.
//	-E_INVAL if filebno is out of range.
//
int
file_get_block(struct File *f, uint32_t filebno, char **blk)
{
	// LAB 5: Your code here.
	//panic("file_block_walk not implemented");
	int		ret		= 0;
	uint32_t	*ppdiskbno	= NULL;
	//bool		alloc		= (f->f_type == FTYPE_DIR)? 0: 1;
	bool		alloc		= 1;

	if (filebno > (NDIRECT + NINDIRECT)) {
		cprintf ("\n\n\n filebno is out of range\n\n\n");
		return -E_INVAL;
	}

	ret = file_block_walk (f, filebno, &ppdiskbno, alloc);
	if (ret < 0) {
		cprintf ("\n\n\n file block walk failed\n\n\n");
		return -E_INVAL;
	}

	*blk = (char *) diskaddr ((uint64_t)*ppdiskbno);

	return 0;
}

// Try to find a file named "name" in dir.  If so, set *file to it.
//
// Returns 0 and sets *file on success, < 0 on error.  Errors are:
//	-E_NOT_FOUND if the file is not found
static int
dir_lookup(struct File *dir, const char *name, struct File **file)
{
	int r;
	uint32_t i, j, nblock;
	char *blk;
	struct File *f;

	// Search dir for name.
	// We maintain the invariant that the size of a directory-file
	// is always a multiple of the file system's block size.
	assert((dir->f_size % BLKSIZE) == 0);
	nblock = dir->f_size / BLKSIZE;
	for (i = 0; i < nblock; i++) {
		if ((r = file_get_block(dir, i, &blk)) < 0)
			return r;
		f = (struct File*) blk;
		for (j = 0; j < BLKFILES; j++)
			if (strcmp(f[j].f_name, name) == 0) {
				*file = &f[j];
				return 0;
			}
	}
	return -E_NOT_FOUND;
}


// Skip over slashes.
static const char*
skip_slash(const char *p)
{
	while (*p == '/')
		p++;
	return p;
}

static  char*
my_skip_slash(char *p)
{
	while (*p == '/')
		p++;
	return p;
}
// Evaluate a path name, starting at the root.
// On success, set *pf to the file we found
// and set *pdir to the directory the file is in.
// If we cannot find the file but find the directory
// it should be in, set *pdir and copy the final path
// element into lastelem.
static int
walk_path(const char *path, struct File **pdir, struct File **pf, char *lastelem)
{
	const char *p;
	char name[MAXNAMELEN];
	struct File *dir, *f;
	int r;

	// if (*path != '/')
	//	return -E_BAD_PATH;
	path = skip_slash(path);
	f = &super->s_root;
	dir = 0;
	name[0] = 0;

	if (pdir)
		*pdir = 0;
	*pf = 0;
	while (*path != '\0') {
		dir = f;
		p = path;
		while (*path != '/' && *path != '\0')
			path++;
		if (path - p >= MAXNAMELEN)
			return -E_BAD_PATH;
		memmove(name, p, path - p);
		name[path - p] = '\0';
		path = skip_slash(path);

		if (dir->f_type != FTYPE_DIR)
			return -E_NOT_FOUND;

		if ((r = dir_lookup(dir, name, &f)) < 0) {
			if (r == -E_NOT_FOUND && *path == '\0') {
				if (pdir)
					*pdir = dir;
				if (lastelem)
					strcpy(lastelem, name);
				*pf = 0;
			}
			return r;
		}
	}

	if (pdir)
		*pdir = dir;
	*pf = f;
	return 0;
}

// --------------------------------------------------------------
// File operations
// --------------------------------------------------------------


// Open "path".  On success set *pf to point at the file and return 0.
// On error return < 0.
int
file_open(const char *path, struct File **pf)
{
	return walk_path(path, 0, pf, 0);
}

// Read count bytes from f into buf, starting from seek position
// offset.  This meant to mimic the standard pread function.
// Returns the number of bytes read, < 0 on error.
ssize_t
file_read(struct File *f, void *buf, size_t count, off_t offset)
{
	int r, bn;
	off_t pos;
	char *blk;

	if (offset >= f->f_size)
		return 0;

	count = MIN(count, f->f_size - offset);

	for (pos = offset; pos < offset + count; ) {
		if ((r = file_get_block(f, pos / BLKSIZE, &blk)) < 0)
			return r;
		bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
		memmove(buf, blk + pos % BLKSIZE, bn);
		pos += bn;
		buf += bn;
	}

	return count;
}

// While implementing writeable FS - predefined prototypes
int
file_write(struct File *f, const void *buf, size_t count, off_t offset)
{
	char *blk;
	int r;
	size_t lcount = 0;
	size_t min;
	int i;
	uint32_t blockno;

	if (debug)
		cprintf("file_write name %s %08x %08x %08x\n", f->f_name, buf, count, offset);

	if (f->f_size == MAXFILESIZE) {
		cprintf ("File reached its max size\n");
		return -1;
	}

	if (offset > f->f_size)
		offset = f->f_size;

	blockno = offset / BLKSIZE;

	/* Get the block to be written to*/
	r = file_get_block (f, blockno, &blk);
	if (r < 0) {
		cprintf ("\nfile get block failed: %e\n", r);
		return r;
	}

	min = MIN (count, PGSIZE - (offset%BLKSIZE));
	memcpy (blk+ (offset % BLKSIZE), buf, min);
	lcount += min;
	if ((r = journal_add(JWRITE, (uintptr_t)blk, 0)) < 0)
		cprintf ("Failed to journal the write\n");

	/*
	if (lcount < count) {
		r = file_get_block (f, blockno+1, &blk);
		if (r < 0) {
			cprintf ("\nfile get block failed: %e\n", r);
			return r;
		}

		min = MIN (count-lcount, PGSIZE);
		memcpy (blk, buf + lcount, min);
		lcount += min;
		f->f_size += lcount;
	}*/
	for (i = 1; lcount < count; i++) {
		r = file_get_block (f, blockno + i, &blk);
		if (r < 0) {
			cprintf ("get block failed: %e\n", r);
			break;
		}

		min = MIN (count, PGSIZE);
		memcpy (blk, buf + lcount, min);
		lcount += min;
		if ((r = journal_add(JWRITE, (uintptr_t)blk, 0)) < 0)
			cprintf ("Failed to journal the write\n");
	}

	if ((r = file_set_size (f, (offset + lcount))) < 0)
		cprintf ("failed to set size\n");
	return lcount;
}

int
file_set_size(struct File *f, off_t newsize)
{
	int r;
	if (newsize < f->f_size) {
		r = handle_otrunc (f, newsize);
		if (r < 0) {
			cprintf ("truncation to lower size failed\n");
			return -1;
		}
	}

	f->f_size = newsize;
	return 0;
}

void
file_flush (struct File *f)
{
	int i = 0;
	int r = 0;
	uint32_t *ib_addr = NULL;

	for (i= 0; i < NDIRECT; i++) {
		if (!f->f_direct[i])
			return;

		r = write_back (f->f_direct[i]);
		if (r < 0) {
			cprintf ("flush failed\n");
			return;
		}
	}

	if (!f->f_indirect)
		return;

	ib_addr = (uint32_t *) diskaddr ((uint64_t)f->f_indirect);

	for (i=0; i < NINDIRECT; i++) {
		if (!ib_addr[i])
			break;

		r = write_back (ib_addr[i]);
		if (r < 0) {
			cprintf ("flush failed on indirect block\n");
			return;
		}
	}

	r = write_back (f->f_indirect);
	if (r < 0)
		cprintf ("flush failed\n");

	return;
}

void
fs_sync(void)
{
	int i;

	for (i = 0; i < NBLOCKS; i++)
		write_back (i);
}

int
file_remove(const char *path)
{
	struct File *f;
	int r = 0;

	r = walk_path (path, 0, &f, 0);
	if (r < 0) {
		cprintf ("Failed to remove file: %e\n", r);
		return r;
	}

	if ((r = journal_add (JREMOVE_FILE, (uintptr_t)f, 0)) < 0)
		cprintf ("Adding entry to journel failed\n");

	r = handle_otrunc (f, 0);
	if (r < 0) {
		cprintf ("Failed to remove file: %e\n", r);
		return r;
	}

	memset (&f, 0x00, sizeof (struct File));
	return 0;
}

// While implementing writeable FS - team defined prototypes
bool
page_is_dirty (char *addr)
{
	if (!((uvpml4e[VPML4E(addr)] & (uint64_t)PTE_P) &&
		(uvpde[VPDPE(addr)] & (uint64_t)PTE_P) &&
		(uvpd[VPD(addr)] & (uint64_t)PTE_P) &&
		(uvpt [PGNUM(addr)] & (uint64_t)PTE_P)))

		if (uvpt[PGNUM(addr)] & (uint64_t)PTE_D)
			return 1;

	return 0;
}

int
write_back (uint32_t blkno)
{
	uint32_t secstart = blkno * BLKSECTS;
	char *addrstart = diskaddr ((uint64_t)blkno);
	int r = 0;

	if (page_is_dirty(addrstart)) {
		r = ide_write (secstart, (void *)addrstart, 4);
		if (r < 0) {
			cprintf ("IDE write failed\n");
			return r;
		}
		mark_page_UNdirty (addrstart);
	}

	return r;
}

void
bitmap_set_free (uint32_t blockno, struct File *f)
{
	int r = 0;
	bitmap[blockno/32] |= (1<<(blockno%32));

	if ((r = write_back (blockof((void *)&bitmap[blockno/32]))) <0)
		cprintf ("Failed to sync block no: %d\n",
				blockof ((void *)&bitmap[blockno/32]));

	if (journal_add(JBITMAP_SET, (uintptr_t)f, (uint64_t)blockno) <0)
		cprintf ("Failed to add to journal\n");
}

// Clears the bit
void
bitmap_clear_flag (uint32_t blockno, struct File *f)
{
	int r = 0;

	bitmap[blockno/32] &= ~(1<<(blockno%32));

	if ((r = write_back (blockof((void *)&bitmap[blockno/32]))) <0)
		cprintf ("Failed to sync block no: %d\n",
				blockof ((void *)&bitmap[blockno/32]));

	if (journal_add(JBITMAP_CLEAR, (uintptr_t)f, (uint64_t)blockno) <0)
		cprintf ("Failed to add to journal\n");
}

bool
is_block_free (uint32_t blockno)
{
	return (bitmap[blockno/32] & (1<<(blockno%32)));
}

uint32_t
blockof(void *pos)
{
	return ((char*)pos - (char *)DISKMAP) / BLKSIZE;
}

uint32_t
get_free_block (void)
{
	uint32_t i;
	int nbitblocks;

	nbitblocks = (NBLOCKS + BLKBITSIZE - 1) / BLKBITSIZE;

	for (i = (2 +nbitblocks +1); i < NBLOCKS; i ++)
		if (is_block_free (i))
			return i;

	return -1;
}

uint32_t
alloc_block (struct File *f)
{
	uint32_t freeblock = 0;

	if ((freeblock = get_free_block ()) < 0)
		panic ("out of memory");

	bitmap_clear_flag (freeblock, f);
	return freeblock;
}

void
bitmap_init (void)
{
	int nbitblocks;
	int i;

	nbitblocks = (NBLOCKS + BLKBITSIZE - 1) / BLKBITSIZE;

	bitmap = (uint32_t *)(DISKMAP + (PGSIZE *2));
	memset(bitmap, 0xFF, nbitblocks * BLKSIZE);

	// Clear the blocks 0, 1, and bitmap stored blocks
	bitmap_clear_flag (0, NULL);
	bitmap_clear_flag (1, NULL);
	for (i = 0; i < nbitblocks; i++)
		bitmap_clear_flag (2+i, NULL);
}
/*
int
skip_to_curdir (char *pathtmp, struct File **pdir, struct File **pf,
		char **ptr)
{
	char *p;
	char name[MAXNAMELEN];
	struct File *dir, *f;
	int r;
	char lpath[MAXPATHLEN];
	char *path = lpath;

	strcpy (path, pathtmp);

	// if (*path != '/')
	//	return -E_BAD_PATH;
	path = my_skip_slash(path);
	f = &super->s_root;
	dir = 0;
	name[0] = 0;

	if (pdir)
		*pdir = 0;
	if (pf)
		*pf = 0;

	while (*path != '\0') {
		dir = f;
		p = path;
		while (*path != '/' && *path != '\0')
			path++;
		if (path - p >= MAXNAMELEN)
			return -E_BAD_PATH;
		memmove(name, p, path - p);
		name[path - p] = '\0';
		path = my_skip_slash(path);

		if (dir->f_type != FTYPE_DIR)
			return -E_NOT_FOUND;

		if ((r = dir_lookup(dir, name, &f)) < 0) {
			if (r == -E_NOT_FOUND) {
				if (pdir)
					*pdir = dir;
				if (ptr)
					*ptr = my_skip_slash (p);
				if (pf)
					*pf = 0;
				r = 0;
			}
			return r;
		}
	}

	if (pdir)
		*pdir = dir;
	*pf = f;
	return 0;
}
*/
int
handle_otrunc (struct File *f, size_t n)
{
	int i = 0;
	uint32_t	*ib_addr	= NULL;
	uint32_t	startpos	= 0;

	if (f->f_type != FTYPE_REG)
		return -1;

	if (n > f->f_size) {
		cprintf ("We currently don't support sparse files\n");
		return -1;
	}

	startpos = (n / BLKSIZE);

	for (i = startpos; i < NDIRECT; i++) {
		if (!f->f_direct[i])
			goto adjustfsize;

		bitmap_set_free (f->f_direct[i], f);
		f->f_direct[i] = 0;
	}

	if (!f->f_indirect)
		goto adjustfsize;

	ib_addr = (uint32_t *) diskaddr ((uint64_t)f->f_indirect);
	i = (startpos > NDIRECT)? startpos: 0;
	for (; i < NINDIRECT; i++) {
		if (!ib_addr[i])
			break;

		bitmap_set_free (ib_addr[i], f);
		ib_addr[i] = 0;
	}

	// Yes it should be <= think about it
	if (startpos <= NDIRECT) {
		// Free the indirect block as well
		bitmap_set_free (f->f_indirect, f);
		f->f_indirect = 0;
	}

adjustfsize:
	f->f_size = n;

	return 0;
}

int
handle_ocreate (char *path, struct File **newfile)
{
	int r = 0;
	char name[MAXNAMELEN] = {0,};
	struct File *dir = NULL;
	struct File *f = NULL;

	r = walk_path (path, &dir, &f, name);
	if (r != -E_NOT_FOUND || dir == NULL) {
		cprintf ("Error on walkpath @handle_ocreate\n");
		return r;
	}

	r = dirent_create (dir, name, FTYPE_REG, newfile);
	if (r < 0) {
		cprintf ("new file creation failed @handle_ocreate\n");
		return r;
	}

	return 0;
}
/*
{
	int r = 0;
	char *ptr = NULL;
	const char *p = NULL;
	struct File *lcurdir = *curdir;
	char creatFile[MAXNAMELEN];
	struct File *newfile;

	if (debug)
		cprintf ("handle_ocreate: path: %s\n", path);

	// not asking to create a file
	if (path [strlen (path) - 1] == '/')
		return -E_BAD_PATH;

	r = skip_to_curdir (path, &lcurdir, 0, &ptr);
	if (r < 0) {
		cprintf ("\nskip to dir failed @handle_ocreate: %e\n", r);
		return r;
	}

	while (*ptr != '\0') {
		p = ptr;
		while (*ptr != '/' && *ptr != '\0')
			ptr ++;

		if (ptr - p >= MAXNAMELEN)
			return -E_BAD_PATH;

		memcpy (creatFile, p, ptr-p);
		creatFile[ptr-p] = '\0';

		if (*ptr == '\0') {
			dirent_create (lcurdir, creatFile, FTYPE_REG, &newfile);
			break;
		} else {
			dirent_create (lcurdir, creatFile, FTYPE_DIR, &newfile);
		}
		lcurdir = newfile;
	}

	*curdir = newfile;
	if (debug)
		cprintf ("curdir: %s\n", (*curdir)->f_name);
	return 0;
}
*/

// Similar to dir_lookup, just looks for ""
int
get_free_dirent (struct File *dir, struct File **file, char **block)
{
	int r;
	uint32_t i, j, nblock;
	char *blk;
	struct File *f;

	// Search dir for name.
	// We maintain the invariant that the size of a directory-file
	// is always a multiple of the file system's block size.
	assert((dir->f_size % BLKSIZE) == 0);
	nblock = dir->f_size / BLKSIZE;
	for (i = 0; i < nblock; i++) {
		if ((r = file_get_block(dir, i, &blk)) < 0)
			return r;
		f = (struct File*) blk;
		for (j = 0; j < BLKFILES; j++)
			if (strcmp (f[j].f_name, "") == 0) {
				*file = &f[j];
				*block = blk;
				return 0;
			}
	}

	return -E_NOT_FOUND;
}

void
mark_page_UNdirty (char *pg)
{
	// TODO: Check whether this is correct
	uint64_t perm = uvpt[PGNUM(pg)] & PTE_SYSCALL;
	int	r = 0;
	// Remove dirty flag
	perm &= ~((uint64_t)PTE_D);

	r = sys_page_map (0, (void *)pg, 0, (void *)pg, perm);
	if (r < 0)
		cprintf ("\nfailed to remove dirty: %e\n", r);
}

/* Given a name and filetype creates a dirent under dir */
static int
dirent_create (struct File *dir, const char *name, uint32_t filetype,
		struct File **newfile)
{
	int r = 0;
	char *pg = NULL;

	if (dir->f_type != FTYPE_DIR) {
		cprintf ("\nCan't create an entry under file\n");
		return -E_BAD_PATH;
	}

	r = get_free_dirent (dir, newfile, &pg);
	if (r < 0) {
		cprintf ("\nFailed to get free dirent: %e\n", r);
		return r;
	}

	strcpy ((*newfile)->f_name, name);
	(*newfile)->f_size = 0;
	(*newfile)->f_type = filetype;

	return 0;
}

/* Journalling */
int
journal_init (void)
{
	int r = 0;
	int i = 0;

	if ((r = openfile_alloc (&jopenfile)) < 0) {
		cprintf ("Failed to create journal file\n");
		return r;
	}

	if (handle_ocreate (JFILE_PATH, &jfile) < 0)
		panic ("Failed to create journal file\n");

	jopenfile->o_file = jfile;
	jopenfile->o_fd->fd_file.id = jopenfile->o_fileid;
	jopenfile->o_fd->fd_omode = O_RDWR;
	jopenfile->o_fd->fd_dev_id = devfile.dev_id;
	jopenfile->o_mode = O_RDWR;

	for (i =0; i <NJBLKS; i++) {
		bitmap_clear_flag (i +JBLK_START, jfile);
		jfile->f_direct[i] = (i +JBLK_START);
	}

	jfile->f_size = NJBLKS *BLKSIZE;
	jfile->f_type = FTYPE_JOURN;
	strcpy (jfile->f_name, JFILE_NAME);
	return 0;
}

int
journal_get_buf (jtype_t jtype, uintptr_t farg, uint64_t sarg, char **buf)
{
	jrdwr_t j;

	switch (jtype) {
	case JBITMAP_SET:
		j.jtype = jtype;
		j.args.jbitmap_set.blockno = (uint64_t)farg;
		j.args.jbitmap_set.structFile = (uintptr_t)sarg;

		if (JOURNAL_ISBINARY)
			goto jbinary;

		snprintf (*buf, MAXJBUFSIZE, "%d:%d:%x\n", j.jtype,
				j.args.jbitmap_set.blockno,
				j.args.jbitmap_set.structFile);
		return strlen (*buf);

	case JBITMAP_CLEAR:
		j.jtype = jtype;
		j.args.jbitmap_clear.blockno = (uint64_t)farg;
		j.args.jbitmap_clear.structFile = (uintptr_t)sarg;

		if (JOURNAL_ISBINARY)
			goto jbinary;

		snprintf (*buf, MAXJBUFSIZE, "%d:%d:%x\n", j.jtype,
				j.args.jbitmap_clear.blockno,
				j.args.jbitmap_clear.structFile);
		return strlen (*buf);

	case JWRITE:
		j.jtype = jtype;
		j.args.jwrite.structFile = (uintptr_t)farg;

		if (JOURNAL_ISBINARY)
			goto jbinary;

		snprintf (*buf, MAXJBUFSIZE, "%d:%x\n", j.jtype,
				j.args.jwrite.structFile);

		return strlen (*buf);

	case JREMOVE_FILE:
		j.jtype = jtype;
		j.args.jremove_file.structFile = (uintptr_t)farg;

		if (JOURNAL_ISBINARY)
			goto jbinary;

		snprintf (*buf, MAXJBUFSIZE, "%d:%x\n", j.jtype,
				j.args.jremove_file.structFile);

		return strlen (*buf);

	default:
		cprintf ("Dude, You're in wrong place\n");
	}

	return -1;

jbinary:
	memcpy (buf, (void *)&j, sizeof (j));
	return sizeof j;
}


int
journal_add (jtype_t jtype, uintptr_t farg, uint64_t sarg)
{
	int r = 0;
	int i = 0;
	char *buf;

	if ((r = journal_get_buf (jtype, farg, sarg, &buf)) < 0) {
		cprintf ("Failed to fill the buf\n");
		return r;
	}

	r = file_write (jfile, (void *)buf, r, jopenfile->o_fd->fd_offset);
	if (r < 0) {
		cprintf ("Failed to add entry to journal: %e\n", r);
		return r;
	}

	jfile->f_direct[NDIRECT -1] = (jopenfile->o_fd->fd_offset += r);

	write_back (blockof ((void *)jfile));
	write_back (jfile->f_direct[NDIRECT -1]);
	for (i =0; i <NJBLKS; i++)
		write_back (jfile->f_direct[i]);

	return 0;
}

/*
int
journal_checkand_repair ()
{
	while
}
*/
