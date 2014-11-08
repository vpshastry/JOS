#include <inc/fs.h>
#include <inc/lib.h>

#define SECTSIZE	512			// bytes per disk sector
#define BLKSECTS	(BLKSIZE / SECTSIZE)	// sectors per block

/* Disk block n, when in memory, is mapped into the file system
 * server's address space at DISKMAP + (n*BLKSIZE). */
#define DISKMAP		0x10000000

/* Maximum disk size we can handle (3GB) */
#define DISKSIZE	0xC0000000

// Whiel implementing writeable FS
uint32_t *bitmap;		// bitmap blocks mapped in memory 1= free, 0 = used
#define NBLOCKS  (DISKSIZE / BLKSIZE)
// <end> writeable FS declaration

struct Super *super;		// superblock
char *diskpos;

/* ide.c */
bool	ide_probe_disk1(void);
void	ide_set_disk(int diskno);
int	ide_read(uint32_t secno, void *dst, size_t nsecs);
int	ide_write(uint32_t secno, const void *src, size_t nsecs);

/* bc.c */
void*	diskaddr(uint64_t blockno);
bool	va_is_mapped(void *va);
bool	va_is_dirty(void *va);
void	flush_block(void *addr);
void	bc_init(void);

/* fs.c */
void	fs_init(void);
int	file_get_block(struct File *f, uint32_t file_blockno, char **pblk);
int	file_create(const char *path, struct File **f);
int	file_open(const char *path, struct File **f);
ssize_t	file_read(struct File *f, void *buf, size_t count, off_t offset);
int	file_write(struct File *f, const void *buf, size_t count, off_t offset);
int	file_set_size(struct File *f, off_t newsize);
void	file_flush(struct File *f);
int	file_remove(const char *path);
void	fs_sync(void);

/* int	map_block(uint32_t); */
bool	block_is_free(uint32_t blockno);
uint32_t alloc_block(void);

/* test.c */
void	fs_test(void);

/* delcared for writeable FS */
void bitmap_set_free (uint32_t blockno);
void bitmap_clear_flag (uint32_t blockno);
uint32_t blockof(void *pos);
uint32_t get_free_block (void);
void bitmap_init (void);
int skip_to_curdir (char *path, const char *curdirname, char **curptr);
int handle_otrunc (struct File *file);
int handle_ocreate (char *path, struct File **curdir);
int get_free_dirent (struct File *dir, struct File **file, char **block);
void mark_page_dirty (char *pg);
static int
dirent_create (struct File *dir, const char *name, uint32_t filetype,
		struct File **newfile);
