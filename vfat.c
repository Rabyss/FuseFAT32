// vim: noet:ts=8:sts=8
#define FUSE_USE_VERSION 26
#define _GNU_SOURCE

#include <sys/mman.h>
#include <assert.h>
#include <endian.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <iconv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vfat.h"

// A kitchen sink for all important data about filesystem
struct vfat_data {
	const char	*dev;
	int		fs;
	struct	fat_boot fb;
	uint32_t fats_offset;
	uint32_t clusters_offset;
	uint32_t cluster_size;
};

struct vfat_data vfat_info;
iconv_t iconv_utf16;

uid_t mount_uid;
gid_t mount_gid;
time_t mount_time;

static void vfat_init(const char *dev)
{
	iconv_utf16 = iconv_open("utf-8", "utf-16"); // from utf-16 to utf-8
	// These are useful so that we can setup correct permissions in the mounted directories
	mount_uid = getuid();
	mount_gid = getgid();

	// Use mount time as mtime and ctime for the filesystem root entry (e.g. "/")
	mount_time = time(NULL);

	vfat_info.fs = open(dev, O_RDONLY);
	if (vfat_info.fs < 0) {
		err(1, "open(%s)", dev);
	}
	
	read(vfat_info.fs, &(vfat_info.fb), 91);
	
	if(!isFAT32(vfat_info.fb)) {
		err(404, "%s is not a FAT32 system\n", dev);
	}
	
	// Helpers
	vfat_info.fats_offset = vfat_info.fb.reserved_sectors * vfat_info.fb.bytes_per_sector;
	printf("reserved sectors: %d\n", vfat_info.fats_offset);
	vfat_info.clusters_offset = vfat_info.fats_offset + (vfat_info.fb.fat32.sectors_per_fat * vfat_info.fb.bytes_per_sector * vfat_info.fb.fat_count);
	vfat_info.cluster_size = vfat_info.fb.sectors_per_cluster * vfat_info.fb.bytes_per_sector;
	
	vfat_readdir(vfat_info.fb.fat32.root_cluster, NULL, NULL);
}

bool isFAT32(struct fat_boot fb) {
	int root_dir_sectors = (fb.root_max_entries*32 + (fb.bytes_per_sector - 1)) / fb.bytes_per_sector;
	
	if(root_dir_sectors != 0) {
		return false;
	}
	
	uint32_t FATSz;
	uint32_t TotSec;
	uint32_t DataSec;
	uint32_t CountofClusters;
	
	if(fb.sectors_per_fat_small != 0) {
		FATSz = fb.sectors_per_fat_small;
	} else {
		FATSz = fb.fat32.sectors_per_fat;
	}
	
	if(fb.total_sectors_small != 0) {
		TotSec = fb.total_sectors_small;
	} else {
		TotSec = fb.total_sectors;
	}
	
	DataSec = TotSec - (fb.reserved_sectors + (fb.fat_count * FATSz) + root_dir_sectors);
	
	CountofClusters = DataSec / fb.sectors_per_cluster;
	
	return CountofClusters >= 65525;
}

static int vfat_readdir(uint32_t first_cluster, fuse_fill_dir_t filler, void *fillerdata)
{
	
	printf("vfat_readdir\n");
	
	struct stat st; // we can reuse same stat entry over and over again
	void *buf = NULL;
	struct vfat_direntry *e;
	char *name;
	
	
	
	u_int32_t offset = first_cluster + vfat_info.clusters_offset - (2 * vfat_info.fb.bytes_per_sector * vfat_info.fb.sectors_per_cluster);
	
	lseek(vfat_info.fs, offset, SEEK_SET);
	struct fat32_direntry_long dir_entry; 
	read(vfat_info.fs, &dir_entry, 33);
	
	printf("\n---\n%lu\n---\n\n", dir_entry.attr);
	
	memset(&st, 0, sizeof(st));
	st.st_uid = mount_uid;
	st.st_gid = mount_gid;
	st.st_nlink = 1;
	
	// Goes through the directory table and calls the filler function on the
	// filler data for each entry (usually the filler is vfat_search_entry)
	/* XXX add your code here */
}

// Used by vfat_search_entry()
struct vfat_search_data { 
	const char	*name;
	int		found;
	struct stat	*st;
};

// You can use this in vfat_resolve as a filler function for vfat_readdir
static int vfat_search_entry(void *data, const char *name, const struct stat *st, off_t offs)
{
	struct vfat_search_data *sd = data;

	if (strcmp(sd->name, name) != 0)
		return (0);

	sd->found = 1;
	*sd->st = *st;

	return (1);
}

// Recursively find correct file/directory node given the path
static int vfat_resolve(const char *path, struct stat *st)
{
	struct vfat_search_data sd;
	// Calls vfat_readdir with vfat_search_entry as a filler
	// and a struct vfat_search_data as fillerdata in order
	// to find each node of the path recursively
	/* XXX add your code here */
	char* token;
	token = strtok(path, "/");
	sd.name = token;
	sd.found = 0;
	/*Find first cluster*/
	//vfat_readdir(cluster, vfat_search_entry, sd);
	return 0; // TODO
}

// Get file attributes
static int vfat_fuse_getattr(const char *path, struct stat *st)
{
	/* XXX: This is example code, replace with your own implementation */
	DEBUG_PRINT("fuse getattr %s\n", path);
	// No such file
	if (strcmp(path, "/")==0) {
		uint32_t rootAddr = vfat_info.fb.fat32.root_cluster;
		
		st->st_dev = 0; // Ignored by FUSE
		st->st_ino = 0; // Ignored by FUSE unless overridden
		st->st_mode = S_IRWXU | S_IRWXG | S_IRWXO | S_IFDIR;
		st->st_nlink = 1;
		st->st_uid = mount_uid;
		st->st_gid = mount_gid;
		st->st_rdev = 0;
		st->st_size = 0;
		st->st_blksize = 0; // Ignored by FUSE
		st->st_blocks = 1;
		return 0;
	}
	if (strcmp(path, "/a.txt")==0 || strcmp(path, "/b.txt")==0) {
		st->st_dev = 0; // Ignored by FUSE
		st->st_ino = 0; // Ignored by FUSE unless overridden
		st->st_mode = S_IRWXU | S_IRWXG | S_IRWXO | S_IFREG;
		st->st_nlink = 1;
		st->st_uid = mount_uid;
		st->st_gid = mount_gid;
		st->st_rdev = 0;
		st->st_size = 10;
		st->st_blksize = 0; // Ignored by FUSE
		st->st_blocks = 1;
		return 0;
	}

	return -ENOENT;
}

static int vfat_fuse_readdir(const char *path, void *buf,
		  fuse_fill_dir_t filler, off_t offs, struct fuse_file_info *fi)
{
	/* XXX: This is example code, replace with your own implementation */
	DEBUG_PRINT("fuse readdir %s\n", path);
	//assert(offs == 0);
	/* XXX add your code here */
	filler(buf, "a.txt", NULL, 0);
	filler(buf, "b.txt", NULL, 0);
	// Calls vfat_resolve to find the first cluster of the directory
	// we wish to read then uses the filler function on all the files
	// in the directory table
	// vfat_resolve(path, NULL); //does not work now
	return 0;
}

static int vfat_fuse_read(const char *path, char *buf, size_t size, off_t offs,
	       struct fuse_file_info *fi)
{
	/* XXX: This is example code, replace with your own implementation */
	DEBUG_PRINT("fuse read %s\n", path);
	assert(size > 1);
	buf[0] = 'X';
	buf[1] = 'Y';
	/* XXX add your code here */
	return 2; // number of bytes read from the file
		  // must be size unless EOF reached, negative for an error 
}

////////////// No need to modify anything below this point
static int vfat_opt_args(void *data, const char *arg, int key, struct fuse_args *oargs)
{
	if (key == FUSE_OPT_KEY_NONOPT && !vfat_info.dev) {
		vfat_info.dev = strdup(arg);
		return (0);
	}
	return (1);
}

static struct fuse_operations vfat_available_ops = {
	.getattr = vfat_fuse_getattr,
	.readdir = vfat_fuse_readdir,
	.read = vfat_fuse_read,
};

int main(int argc, char **argv)
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	fuse_opt_parse(&args, NULL, NULL, vfat_opt_args);
	
	if (!vfat_info.dev)
		errx(1, "missing file system parameter");

	vfat_init(vfat_info.dev);
	return (fuse_main(args.argc, args.argv, &vfat_available_ops, NULL));
}