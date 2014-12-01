#include <inc/fs.h>
#include <inc/lib.h>

#define SECTSIZE	512			// bytes per disk sector
#define BLKSECTS	(BLKSIZE / SECTSIZE)	// sectors per block

/* Disk block n, when in memory, is mapped into the file system
 * server's address space at DISKMAP + (n*BLKSIZE). */
#define DISKMAP		0x10000000

/* Maximum disk size we can handle (3GB) */
#define DISKSIZE	0xC0000000

/* From serv.c */

// Whiel implementing writeable FS
uint32_t *bitmap; // bitmap blocks mapped in memory 1= free, 0 = used
#define NBLOCKS  (super->s_nblocks)
// Journalling
struct File journalFile;
struct File *jfile;
#define TRUE		1
#define FALSE		0
#define NJBLKS		(2)
#define JBLK_START 	(NBLOCKS -NJBLKS -1)
#define JBSTART_ADDR	(BLKSIZE *JBLK_START)
#define FTYPE_JOURN	0x10
#define MAXJBUFSIZE	512
#define JFILE_NAME	".journal"
#define JFILE_PATH	"/"JFILE_NAME
#define JOURNAL_ISBINARY (FALSE)
#define E_NEEDS_SCANNING -2
typedef enum {
	JWRITE,
	JREMOVE_FILE,
	JBITMAP_CLEAR,
	JBITMAP_SET,
	JDONE,
} jtype_t;

typedef struct {
	jtype_t	jtype;
	union {
		struct {
			uintptr_t structFile;
		} jwrite;
		struct {
			uintptr_t structFile;
		} jremove_file;
		struct {
			uintptr_t structFile;
		} jdone;
		struct {
			uint64_t blockno;
			uintptr_t structFile;
		} jbitmap_clear;
		struct {
			uint64_t blockno;
			uintptr_t structFile;
		} jbitmap_set;
	} args;
} jrdwr_t;

// <end> writeable FS declaration and journalling

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
bool	is_block_free(uint32_t blockno);
uint32_t alloc_block(struct File *f);

/* test.c */
void	fs_test(void);

/* delcared for writeable FS */
void bitmap_set_free (uint32_t blockno, struct File *);
void bitmap_clear_flag (uint32_t blockno, struct File *f);
uint32_t blockof(void *pos);
uint32_t get_free_block (void);
void bitmap_init (void);
int skip_to_curdir (char *pathtmp, struct File **pdir, struct File **pf,
		char **ptr);
int handle_otrunc (struct File *file, size_t n);
int handle_ocreate (char *path, struct File **curdir);
int get_free_dirent (struct File *dir, struct File **file, char **block);
void mark_page_UNdirty (char *pg);
void mark_page_dirty (char *pg);
static int
dirent_create (struct File *dir, const char *name, uint32_t filetype,
		struct File **newfile);
int write_back (uint32_t blkno);

/* Journal functions */
int journal_add (jtype_t jtype, uintptr_t farg, uint64_t sarg);
int journal_get_buf (jtype_t jtype, uintptr_t farg, uint64_t sarg, char *buf);
int journal_init (void);
int journal_scan_and_recover (void);
int journal_file_write(struct File *f, const void *buf, size_t count,
			off_t offset);
struct File * journal_get_fp (jrdwr_t *jentry);
int journal_check_matching_done (int idx, int *array, jrdwr_t *jarray, int end, bool *skip_array);
int journal_recover_file (int *array, int len, jrdwr_t *jarray);
