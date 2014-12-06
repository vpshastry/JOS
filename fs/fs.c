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
	cprintf ("Starting FS\n");
	static_assert(sizeof(struct File) == 256);
	int r = 0;

	// Find a JOS disk.  Use the second IDE disk (number 1) if available.
	if (ide_probe_disk1())
		ide_set_disk(1);
	else
		ide_set_disk(0);

	bc_init();

	// Set "super" to point to the super block.
	super = diskaddr(1);
	check_super();

	//if (strcmp (super->journalFile.f_name, JFILE_NAME))
		bitmap_init ();

	if ((r = journal_init ()) < 0) {
		if (r != -E_NEEDS_SCANNING)
			cprintf ("Initializing journal failed\n");

		cprintf ("Trying to recover the old state\n");
		if ((r = journal_scan_and_recover ()) < 0) {
			cprintf ("Failed to recover\n");
		}
	}
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

	//cprintf ("Fbno: %d, disk b no: %d\n", filebno, **ppdiskbno);
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
	if (!strcmp (path, JFILE_PATH)) {
		*pf = &super->journalFile;
		return 0;
	}
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
		cprintf("file_write name %s %s %08x %08x\n", f->f_name, buf, count, offset);

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
	//if ((r = journal_add(JWRITE, (uintptr_t)blk, 0)) < 0)
		//cprintf ("Failed to journal the write\n");
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
		//if ((r = journal_add(JWRITE, (uintptr_t)blk, 0)) < 0)
			//cprintf ("Failed to journal the write\n");
	}

	if ((r = file_set_size (f, MAX(offset + lcount, f->f_size))) < 0)
		cprintf ("failed to set size\n");

	if (journal_add (JDONE, (uintptr_t)f, 0) < 0)
		cprintf ("Adding journal failed\n");

	//cprintf ("returning\n");
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
	write_back (blockof (f));
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
			continue;

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
			continue;

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

	for (i =1; i <NBLOCKS; i++) {
		if (!(bitmap[i/32] & (1<<(i%32)))) {
			write_back (i);
		}
	}
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

	if (f->f_type == FTYPE_DIR || f->f_type == FTYPE_JOURN)
		return E_BAD_PATH;

	if ((r = journal_add (JSTART, (uintptr_t)f, 0)) < 0)
		cprintf ("Adding entry to journel failed\n");

	if ((r = journal_add (JREMOVE_FILE, (uintptr_t)f, 0)) < 0)
		cprintf ("Adding entry to journel failed\n");

	r = handle_otrunc (f, 0);
	if (r < 0) {
		cprintf ("Failed to remove file: %e\n", r);
		return r;
	}

	if (journal_add (JDONE, (uintptr_t)f, 0) < 0)
		cprintf ("Adding journal failed\n");

	memset (f, 0x00, sizeof (struct File));
	write_back (blockof (f));

	return 0;
}

// While implementing writeable FS - team defined prototypes
bool
page_is_present (void *addr)
{
	return (((uvpml4e[VPML4E(addr)] & (uint64_t)PTE_P) &&
		(uvpde[VPDPE(addr)] & (uint64_t)PTE_P) &&
		(uvpd[VPD(addr)] & (uint64_t)PTE_P) &&
		(uvpt [PGNUM(addr)] & (uint64_t)PTE_P)));
}

bool
page_is_dirty (void *addr)
{
	if (page_is_present (addr))
		if ((uvpt[PGNUM(addr)] & PTE_D) != 0)
			return 1;

	return 0;
}

int
write_back (uint32_t blkno)
{
	uint32_t secstart = blkno * BLKSECTS;
	char *addrstart = diskaddr ((uint64_t)blkno);
	int r = 0;
	int k = 0;

	if (page_is_dirty(addrstart)) {
		for (k =0; k <BLKSECTS; k++, secstart += SECTSIZE) {
			r = ide_write (secstart, (void *)addrstart, 1);
			if (r < 0) {
				cprintf ("IDE write failed\n");
				return r;
			}
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

	if (!f || f->f_type == FTYPE_JOURN)
		return;

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

	if (!f || f->f_type == FTYPE_JOURN)
		return;

	if (journal_add(JBITMAP_CLEAR, (uintptr_t)f, (uint64_t)blockno) <0)
		cprintf ("Failed to add to journal\n");

	if (crash_testing) {
		crash_testing = 0;
		fs_sync();
		panic ("Crash testing\n");
	}
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
check_bitmap(void)
{
	uint32_t i;

	// Make sure all bitmap blocks are marked in-use
	for (i = 0; i * BLKBITSIZE < super->s_nblocks; i++)
		assert(!is_block_free(2+i));

	// Make sure the reserved and root blocks are marked in-use.
	assert(!is_block_free(0));
	assert(!is_block_free(1));

	cprintf("bitmap is good\n");
}

void
bitmap_init (void)
{
	bitmap = (uint32_t *)(DISKMAP + (PGSIZE *2));
	return;
	int nbitblocks;
	int i;

	nbitblocks = (NBLOCKS + BLKBITSIZE - 1) / BLKBITSIZE;

	bitmap = (uint32_t *)(DISKMAP + (PGSIZE *2));

	memset(bitmap, 0xFFFFFFFF, BLKSIZE);

	// Clear the blocks 0, 1, and bitmap stored blocks
	bitmap_clear_flag (0, NULL);
	bitmap_clear_flag (1, NULL);
	for (i = 0; i < nbitblocks; i++)
		bitmap_clear_flag (2+i, NULL);

	//check_bitmap();
}

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
	return;
	// TODO: Check whether this is correct
	int perm = uvpt[PGNUM(pg)] & PTE_SYSCALL;
	int	r = 0;
	// Remove dirty flag
	//perm &= ~((uint64_t)PTE_D);
	//int perm = PTE_SYSCALL;	

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

	//cprintf ("blockof: %d\n", blockof (*newfile));
	write_back (blockof(*newfile));
	return 0;
}

/* Journalling */
int
journal_init (void)
{
	int r = 0;
	int i = 0;
	uint64_t start = super->jstart;
	uint64_t end = super->jend;
	jfile = &super->journalFile;
	crash_testing = 0;

	if (!strcmp (super->journalFile.f_name, JFILE_NAME))
		cprintf ("$@##@$T&@#$*&#$&T*&#*&^#@$*&^#Persists\n");
	jfile->f_size = NJBLKS *BLKSIZE;
	jfile->f_type = FTYPE_JOURN;
	strcpy (jfile->f_name, JFILE_NAME);

	for (i =0; i <NJBLKS; i++) {
		bitmap_clear_flag (i +JBLK_START, jfile);
		jfile->f_direct[i] = (i +JBLK_START);
	}

	if (start != 0 || end != 0)
		return E_NEEDS_SCANNING;

	cprintf ("Returning 0\n");
	return 0;
}

int
journal_get_buf (jtype_t jtype, uintptr_t farg, uint64_t sarg, char *buf, int *ref)
{
	jrdwr_t j;

	switch (jtype) {
	case JBITMAP_SET:
		j.jtype = jtype;
		j.jref = (((struct File *)farg)->jref);
		j.args.jbitmap_set.structFile = (uintptr_t)farg;
		j.args.jbitmap_set.blockno = (uint64_t)sarg;

		if (JOURNAL_ISBINARY)
			goto jbinary;

		snprintf (buf, MAXJBUFSIZE, "%d:%u:%x:%d\n", j.jtype, j.jref,
				j.args.jbitmap_set.structFile,
				j.args.jbitmap_set.blockno);
		*ref = j.jref;
		return strlen (buf);

	case JBITMAP_CLEAR:
		j.jtype = jtype;
		j.jref = (((struct File *)farg)->jref);
		j.args.jbitmap_clear.structFile = (uintptr_t)farg;
		j.args.jbitmap_clear.blockno = (uint64_t)sarg;

		if (JOURNAL_ISBINARY)
			goto jbinary;

		snprintf (buf, MAXJBUFSIZE, "%d:%u:%x:%d\n", j.jtype, j.jref,
				j.args.jbitmap_clear.structFile,
				j.args.jbitmap_clear.blockno);
		*ref = j.jref;
		return strlen (buf);

	case JSTART:
		j.jtype = jtype;
		j.jref = (++((struct File *)farg)->jref);
		j.args.jstart.structFile = (uintptr_t)farg;

		if (JOURNAL_ISBINARY)
			goto jbinary;

		snprintf (buf, MAXJBUFSIZE, "%d:%u:%x\n", j.jtype, j.jref,
				j.args.jstart.structFile);

		*ref = j.jref;
		return strlen (buf);

	case JREMOVE_FILE:
		j.jtype = jtype;
		j.jref = (((struct File *)farg)->jref);
		j.args.jremove_file.structFile = (uintptr_t)farg;

		if (JOURNAL_ISBINARY)
			goto jbinary;

		snprintf (buf, MAXJBUFSIZE, "%d:%u:%x\n", j.jtype, j.jref,
				j.args.jremove_file.structFile);

		*ref = j.jref;
		return strlen (buf);

	case JDONE:
		j.jtype = jtype;
		j.jref = (--((struct File *)farg)->jref);
		j.args.jdone.structFile = (uintptr_t)farg;

		if (JOURNAL_ISBINARY)
			goto jbinary;

		snprintf (buf, MAXJBUFSIZE, "%d:%u:%x\n", j.jtype, j.jref,
				j.args.jdone.structFile);

		*ref = j.jref;
		return strlen (buf);

	default:
		cprintf ("Dude, You're in wrong place\n");
	}

	return -1;

jbinary:
	memcpy (buf, (void *)&j, sizeof (j));
	*ref = j.jref;
	return sizeof j;
}

int
journal_file_get_block (struct File *f, uint32_t filebno, char **blk)
{
	int		ret		= 0;
	uint32_t	*ppdiskbno	= NULL;

	if (filebno > (NDIRECT + NINDIRECT)) {
		cprintf ("\n\n\n filebno is out of range\n\n\n");
		return -E_INVAL;
	}

	ret = file_block_walk (f, filebno, &ppdiskbno, 0);
	if (ret < 0) {
		cprintf ("\n\n\n file block walk failed\n\n\n");
		return -E_INVAL;
	}

	*blk = (char *) diskaddr ((uint64_t)*ppdiskbno);

	return 0;
}

int
journal_file_write(struct File *f, const void *buf, size_t count,
			off_t offset)
{
	char *blk;
	int r;
	size_t lcount = 0;
	size_t min;
	int i;
	uint32_t blockno;

	if (debug)
		cprintf("file_write name %s %s %08x %08x\n", f->f_name, buf, count, offset);

	if (f->f_size == MAXFILESIZE) {
		cprintf ("File reached its max size\n");
		return -1;
	}

	if (offset > f->f_size)
		offset = f->f_size;

	blockno = offset / BLKSIZE;

	/* Get the block to be written to*/
	r = journal_file_get_block (f, blockno, &blk);
	if (r < 0) {
		cprintf ("\nfile get block failed: %e\n", r);
		return r;
	}

	min = MIN (count, PGSIZE - (offset%BLKSIZE));
	memcpy (blk+ (offset % BLKSIZE), buf, min);
	lcount += min;

	if (lcount < count) {
		r = journal_file_get_block (f, blockno+1, &blk);
		if (r < 0) {
			cprintf ("\nfile get block failed: %e\n", r);
			return r;
		}

		min = MIN (count-lcount, PGSIZE);
		memcpy (blk, buf + lcount, min);
		lcount += min;
		f->f_size += lcount;
	}

	//cprintf ("jfile size: %d\n", jfile->f_size);
	   if ((r = file_set_size (f, MAX(offset + lcount, f->f_size))) < 0)
		cprintf ("failed to set size\n");
	return lcount;
}

int
journal_add (jtype_t jtype, uintptr_t farg, uint64_t sarg)
{
	//return 0;
	int r = 0;
	int i = 0;
	char buf[512];
	uint64_t *start = &super->jstart;
	uint64_t *end = &super->jend;
	int ref =0;

	if (((struct File *)farg)->f_type == FTYPE_JOURN)
		return 0;

	//cprintf ("before get buf\n");
	if ((r = journal_get_buf (jtype, farg, sarg, buf, &ref)) < 0) {
		cprintf ("Failed to fill the buf\n");
		return r;
	}
	//cprintf ("name: %s, buf: %s, off: %d\n", ((struct File *)farg)->f_name, buf, *end);

	r = journal_file_write (jfile, (void *)buf, r, *end);
	if (r < 0) {
		cprintf ("Failed to add entry to journal: %e\n", r);
		return r;
	}

	if (*end >= (NJBLKS -1)*PGSIZE)
		*end = 0;
	else
		*end += r;

	if (*start >= *end && *start <= (*end +r))
		*start = *end +r;

	if (jtype == JDONE && ref == 0) {
		if (!JOURNAL_ISBINARY) {
			strcpy (buf, "CheckPointing\n");
			r = journal_file_write (jfile, (void *)buf, strlen("CheckPointing\n"), *end);
			*end += r;
		}
		fs_sync ();
	} else {
		write_back (blockof ((void *)jfile));
		write_back (1); // We store start and end in superblock so sync
		write_back (blockof (bitmap));
		for (i =0; i <NJBLKS; i++)
			write_back (jfile->f_direct[i]);
	}

	//cprintf ("Crash: %d\n", crash_testing);
	if (crash_testing) {
		crash_testing = 0;
		panic ("Testing the crash recovery functionality\n");
	}

	return 0;
}

int
journal_scan_and_recover(void)
{
	//cprintf ("------------------->Here?\n");
	if (!super->jstart && !super->jend)
		return 0;

	uint64_t *start = &super->jstart;
	uint64_t *end = &super->jend;
	// Is a copy of the journal file (separate buf to access it continuously
	char bufcopy[NJBLKS *PGSIZE];
	jrdwr_t *jarray = (jrdwr_t *) bufcopy;
	int len = 0;
	int i = 0;
	int r = 0;
	bool skip_array[(NJBLKS *BLKSIZE)/sizeof (jrdwr_t)] = {0,};

	//if (!super->jstart && !super->jend)
		//return 0;

	if (*start >= *end) {
		len = (((NJBLKS *BLKSIZE) +JBSTART_ADDR) - *start);
		memcpy (bufcopy, start, len);
		memcpy ((void *)(bufcopy +len),
				(void *)((uint64_t)JBSTART_ADDR), *end);
		len += *end;
	} else {
		len = *end;
		memcpy (bufcopy, start, *end - *start);
	}

	// Used to store the index of the entry that are related to file
	// that needs recovery
	int array[len];

	for (i = 0; i < (len / sizeof (jrdwr_t)); i++) {
		cprintf ("%d:%p\n", jarray[i].jtype,
				journal_get_fp (&jarray[i]));
		if (skip_array[i])
			continue;

		if (jarray[i].jtype == JDONE)
			continue;

		if ((r = journal_check_matching_done (i, array, jarray, len,
							skip_array)) < 0)
			journal_recover_file (array, -r, jarray);
	}
	//panic ("not yet implemetned");
	return 0;
}

int
journal_rec_remove (int *array, int len, jrdwr_t *jarray)
{
	struct File *remFile = journal_get_fp (&jarray[0]);

	memset (remFile, 0x00, sizeof (struct File));

	return 0;
}
int
journal_rec_bitmapclear(int *array, int len, jrdwr_t *jarray)
{
	return 0;
}
int
journal_rec_bitmapset(int *array, int len, jrdwr_t *jarray)
{
	return 0;
}

int
journal_recover_file (int *array, int len, jrdwr_t *jarray)
{
	int i = 0;

	for (i = 0; i < len; i++) {
		switch (jarray[array[i]].jtype) {
		case JREMOVE_FILE:
			journal_rec_remove (array, len, jarray);
			return 0;
		case JBITMAP_SET:
			journal_rec_bitmapset (array, len, jarray);
			return 0;
		case JBITMAP_CLEAR:
			journal_rec_bitmapclear (array, len, jarray);
			return 0;
		default:
			cprintf ("Don't know dude\n");
		}
	}
	return 0;
}

int
journal_check_matching_done (int idx, int *array, jrdwr_t *jarray, int end,
				bool *skip_array)
{
	struct File *f = journal_get_fp (&jarray[idx]);
	int i = 0;
	int k = 0;

	for (i = idx; i < end; i++) {
		if (f != journal_get_fp(&jarray[i]))
			continue;

		if (jarray[i].jtype == JDONE && jarray[i].jref == 0)
			return 0;

		skip_array[i] = true, array[k++] = i;
	}
	return -k;
}

struct File *
journal_get_fp (jrdwr_t *jentry)
{
	switch (jentry->jtype) {
	case JSTART:
		return (struct File *) jentry->args.jstart.structFile;
	case JREMOVE_FILE:
		return (struct File *) jentry->args.jremove_file.structFile;
	case JBITMAP_SET:
		return (struct File *) jentry->args.jbitmap_set.structFile;
	case JBITMAP_CLEAR:
		return (struct File *) jentry->args.jbitmap_clear.structFile;
	default:
		cprintf ("I don't know how to resolve this\n");
	}
	return 0;
}
