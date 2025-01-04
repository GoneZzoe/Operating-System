/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid, Angela Demke Brown
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2022 Angela Demke Brown
 */

/**
 * CSC369 Assignment 4 - vsfs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "vsfs.h"
#include "fs_ctx.h"
#include "options.h"
#include "util.h"
#include "bitmap.h"
#include "map.h"

//NOTE: All path arguments are absolute paths within the vsfs file system and
// start with a '/' that corresponds to the vsfs root directory.
//
// For example, if vsfs is mounted at "/tmp/my_userid", the path to a
// file at "/tmp/my_userid/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "/tmp/my_userid/dir/" will be passed to
// FUSE callbacks as "/dir".


/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool vsfs_init(fs_ctx *fs, vsfs_opts *opts)
{
	size_t size;
	void *image;
	
	// Nothing to initialize if only printing help
	if (opts->help) {
		return true;
	}

	// Map the disk image file into memory
	image = map_file(opts->img_path, VSFS_BLOCK_SIZE, &size);
	if (image == NULL) {
		return false;
	}

	return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in vsfs_init().
 */
static void vsfs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx*)ctx;
	if (fs->image) {
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}


/* Returns the inode number for the element at the end of the path
 * if it exists.  If there is any error, return -1.
 * Possible errors include:
 *   - The path is not an absolute path
 *   - An element on the path cannot be found
 */
static int path_lookup(const char *path, vsfs_dentry **dirt) {
	if(path[0] != '/') {
		fprintf(stderr, "Not an absolute path\n");
		return -ENOSYS;
	} 

	// TODO: complete this function and any helper functions
	if (strcmp(path, "/") == 0) {
		return 0;
	}

	fs_ctx *fs = get_fs();
	// getting the root inode
	vsfs_inode *root_inode = &(fs->itable[VSFS_ROOT_INO]);
	vsfs_dentry *dt;

	int dentry_per_blk = VSFS_BLOCK_SIZE / sizeof(vsfs_dentry);
	// find the data region of single indirect ptr.
	vsfs_blk_t *indirect_ptr_lst = (vsfs_blk_t *)(fs->image + VSFS_BLOCK_SIZE * (root_inode->i_indirect));
	// iterate through data region that stores entries.
	for (vsfs_blk_t i = 0; i < root_inode->i_blocks; i ++)
	{
		if (i < VSFS_NUM_DIRECT) // direct access.
		{
			dt = (vsfs_dentry *)(fs->image + VSFS_BLOCK_SIZE * (root_inode->i_direct[i]));
			for (int j = 0; j < dentry_per_blk; j ++)
			{
				if (strcmp(dt[j].name, path + 1) == 0) 
				{
					*dirt = &(dt[j]);
					return dt[j].ino;
				}
			}
		}
		else // indirect access
		{
			vsfs_blk_t dentry_idx = indirect_ptr_lst[i - VSFS_NUM_DIRECT];
			dt = (vsfs_dentry *)(fs->image + VSFS_BLOCK_SIZE * dentry_idx);
			for (int j = 0; j < dentry_per_blk; j ++)
			{
				if (strcmp(dt[j].name, path + 1) == 0) 
				{
					*dirt = &(dt[j]);
					return dt[j].ino;
				}
			}
		}
	}
	return -ENOSYS;
}

/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int vsfs_statfs(const char *path, struct statvfs *st)
{
	(void)path;// unused
	fs_ctx *fs = get_fs();
	vsfs_superblock *sb = fs->sb; /* Get ptr to superblock from context */
	
	memset(st, 0, sizeof(*st));
	st->f_bsize   = VSFS_BLOCK_SIZE;   /* Filesystem block size */
	st->f_frsize  = VSFS_BLOCK_SIZE;   /* Fragment size */
	// The rest of required fields are filled based on the information 
	// stored in the superblock.
        st->f_blocks = sb->sb_num_blocks;     /* Size of fs in f_frsize units */
        st->f_bfree  = sb->sb_free_blocks;    /* Number of free blocks */
        st->f_bavail = sb->sb_free_blocks;    /* Free blocks for unpriv users */
	st->f_files  = sb->sb_num_inodes;     /* Number of inodes */
        st->f_ffree  = sb->sb_free_inodes;    /* Number of free inodes */
        st->f_favail = sb->sb_free_inodes;    /* Free inodes for unpriv users */

	st->f_namemax = VSFS_NAME_MAX;     /* Maximum filename length */

	return 0;
}

/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors);
 *       it should include any metadata blocks that are allocated to the 
 *       inode (for vsfs, that is the indirect block). 
 *
 * NOTE2: the st_mode field must be set correctly for files and directories.
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int vsfs_getattr(const char *path, struct stat *st)
{
	if (strlen(path) >= VSFS_PATH_MAX) return -ENAMETOOLONG;
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));

	//NOTE: This is just a placeholder that allows the file system to be 
	//      mounted without errors.
	//      You should remove this from your implementation.
	// if (strcmp(path, "/") == 0) {		
	// 	//NOTE: all the fields set below are required and must be set 
	// 	// using the information stored in the corresponding inode
	// 	st->st_ino = 0;
	// 	st->st_mode = S_IFDIR | 0777;
	// 	st->st_nlink = 2;
	// 	st->st_size = 0;
	// 	st->st_blocks = 0 * VSFS_BLOCK_SIZE / 512;
	// 	st->st_mtim = (struct timespec){0};
	// 	return 0;
	// }

	//TODO: lookup the inode for given path and, if it exists, fill in the
	// required fields based on the information stored in the inode
	(void)fs;
	// vsfs_blk_t ino;
	vsfs_dentry * dirt;
	int inode_num = path_lookup(path, &dirt);
	if (inode_num < 0) return -ENOENT;

	vsfs_inode *inode = &(fs->itable[inode_num]);
	// copy imformation.
	st->st_mode = inode ->i_mode;
	st->st_nlink = inode ->i_nlink;
	st->st_size = inode->i_size;
	st->st_blocks = inode->i_blocks * VSFS_BLOCK_SIZE / 512;
	st->st_mtim = inode->i_mtime;
	// every time getattr called, add the indirect block.
	if (inode->i_blocks > VSFS_NUM_DIRECT) st->st_blocks += VSFS_BLOCK_SIZE / 512;

	return 0;
}

/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int vsfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
	(void)offset;// unused
	(void)fi;// unused
	fs_ctx *fs = get_fs();
	//NOTE: This is just a placeholder that allows the file system to be mounted
	// without errors. You should remove this from your implementation.
	// if (strcmp(path, "/") == 0) {
	// 	filler(buf, "." , NULL, 0);
	// 	filler(buf, "..", NULL, 0);
	// 	return 0;
	// }
	//TODO: lookup the directory inode for the given path and iterate 
	//      through its directory entries
	(void)fs;
	(void) path;
	int dentry_per_blk = VSFS_BLOCK_SIZE / sizeof(vsfs_dentry);
	// get the root inode since only 1 directory.
	vsfs_inode *root = &(fs->itable[VSFS_ROOT_INO]);
	vsfs_dentry *dt;
	// get the indirect block ptr array.
	vsfs_blk_t *indirect_ptr_lst = (vsfs_blk_t *)(fs->image + VSFS_BLOCK_SIZE * (root->i_indirect));
	// read through all the dentry for root inode.
	for (vsfs_blk_t i = 0; i < root->i_blocks; i ++)
	{
		if (i < VSFS_NUM_DIRECT) // direct access.
		{
			dt = (vsfs_dentry *)(fs->image + VSFS_BLOCK_SIZE * (root->i_direct[i]));
			for (int j = 0; j < dentry_per_blk; j ++)
			{
				if (dt[j].ino == VSFS_INO_MAX) continue;
				if (filler(buf, dt[j].name, NULL, 0) != 0) return -ENOMEM;
			}
		}
		else // indirect access
		{
			vsfs_blk_t dentry_idx = indirect_ptr_lst[i - VSFS_NUM_DIRECT];
			dt = (vsfs_dentry *)(fs->image + VSFS_BLOCK_SIZE * dentry_idx);
			for (int j = 0; j < dentry_per_blk; j ++)
			{
				if (dt[j].ino == VSFS_INO_MAX) continue;
				if (filler(buf, dt[j].name, NULL, 0) != 0) return -ENOMEM;
			}
		}
	}
	return 0;
}

static int create_dentry_for_newfile(uint32_t inode_idx, const char* path)
{
	fs_ctx *fs = get_fs();
	int dentry_per_blk = VSFS_BLOCK_SIZE / sizeof(vsfs_dentry);
	// get the root inode since only 1 directory.
	vsfs_inode *root = &(fs->itable[VSFS_ROOT_INO]);
	vsfs_dentry *dt;
	vsfs_blk_t *indirect_ptr_lst;
	// get the indirect block ptr array.
	if (root->i_blocks > VSFS_NUM_DIRECT) indirect_ptr_lst = (vsfs_blk_t *)(fs->image + VSFS_BLOCK_SIZE * (root->i_indirect));
	// read through all the dentry for root inode.
	// first go through all allocated data block try to find an empty block.
	for (vsfs_blk_t i = 0; i < root->i_blocks; i ++)
	{
		if (i < VSFS_NUM_DIRECT)
		{
			dt = (vsfs_dentry *)(fs->image + VSFS_BLOCK_SIZE * (root->i_direct[i]));
			for (int j = 0; j < dentry_per_blk; j ++)
			{
				if (dt[j].ino == VSFS_INO_MAX)
				{
					dt[j].ino = inode_idx;
					strncpy(dt[j].name, path + 1, VSFS_NAME_MAX);
					return 0;
				}
			}
		}
		else
		{
			vsfs_blk_t dentry_idx = indirect_ptr_lst[i - VSFS_NUM_DIRECT];
			dt = (vsfs_dentry *)(fs->image + VSFS_BLOCK_SIZE * dentry_idx);
			for (int j = 0; j < dentry_per_blk; j ++)
			{
				if (dt[j].ino == VSFS_INO_MAX)
				{
					dt[j].ino = inode_idx;
					strncpy(dt[j].name, path + 1, VSFS_NAME_MAX);
					return 0;
				}
			}
		}
	}
	// if there is no empty dentry for allocated data region, need to allocate new datablock.
	uint32_t dt_idx;
	if (root->i_blocks < VSFS_NUM_DIRECT)
	{
		bitmap_alloc(fs->dbmap, fs->sb->sb_num_blocks, &dt_idx);
		bitmap_set(fs->dbmap, fs->sb->sb_num_blocks, dt_idx, true);
		root->i_direct[root->i_blocks] = dt_idx;
		memset(fs->image + VSFS_BLOCK_SIZE * dt_idx , 0 , VSFS_BLOCK_SIZE);
		dt = (vsfs_dentry *)(fs->image + VSFS_BLOCK_SIZE * (dt_idx));
		dt[0].ino = inode_idx;
		strncpy(dt[0].name, path + 1, VSFS_NAME_MAX);
		for (int i = 1; i < dentry_per_blk; i ++) dt[i].ino = VSFS_INO_MAX;
		root->i_blocks ++;
		root->i_size += VSFS_BLOCK_SIZE;
		fs->sb->sb_free_blocks --;
		return 0;
	}
	else if (root->i_blocks == VSFS_NUM_DIRECT)
	{
		// if just full, first allocate an indirect block.
		uint32_t ptr_dt_idx;
		bitmap_alloc(fs->dbmap, fs->sb->sb_num_blocks, &ptr_dt_idx);
		if (bitmap_alloc(fs->dbmap, fs->sb->sb_num_blocks, &dt_idx) != 0) return -ENOSPC;
		bitmap_set(fs->dbmap, fs->sb->sb_num_blocks, ptr_dt_idx, true);
		bitmap_set(fs->dbmap, fs->sb->sb_num_blocks, dt_idx, true);
		memset(fs->image + VSFS_BLOCK_SIZE * ptr_dt_idx, 0 , VSFS_BLOCK_SIZE);
		memset(fs->image + VSFS_BLOCK_SIZE * dt_idx, 0, VSFS_BLOCK_SIZE);
		root->i_indirect = ptr_dt_idx;
		indirect_ptr_lst = (vsfs_blk_t *)(fs->image + VSFS_BLOCK_SIZE * (ptr_dt_idx));
		indirect_ptr_lst[0] = dt_idx;
		dt = (vsfs_dentry *)(fs->image + VSFS_BLOCK_SIZE * (dt_idx));
		dt[0].ino = inode_idx;
		strncpy(dt[0].name, path + 1, VSFS_NAME_MAX);
		for (int i = 1; i < dentry_per_blk; i ++) dt[i].ino = VSFS_INO_MAX;
		root->i_blocks ++;
		root->i_size += VSFS_BLOCK_SIZE;
		fs->sb->sb_free_blocks --;
		fs->sb->sb_free_blocks --;
		// indirect pointer block takes up an addtional data block.
		return 0;
	} else if (root->i_blocks < 1029) // 5 + 1024 MAX_FILE LIMIT.
	{
		bitmap_alloc(fs->dbmap, fs->sb->sb_num_blocks, &dt_idx);
		bitmap_set(fs->dbmap, fs->sb->sb_num_blocks, dt_idx, true);
		indirect_ptr_lst = (vsfs_blk_t *)(fs->image + VSFS_BLOCK_SIZE * (root->i_indirect));
		indirect_ptr_lst[root->i_blocks - VSFS_NUM_DIRECT] = dt_idx;
		dt = (vsfs_dentry *)(fs->image + VSFS_BLOCK_SIZE * (dt_idx));
		dt[0].ino = inode_idx;
		strncpy(dt[0].name, path + 1, VSFS_NAME_MAX);
		for (int i = 1; i < dentry_per_blk; i ++) dt[i].ino = VSFS_INO_MAX;
		root->i_blocks ++;
		root->i_size += VSFS_BLOCK_SIZE;
		fs->sb->sb_free_blocks --;
		return 0;
	}

	return -ENOSPC;
}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int vsfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;// unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();

	//TODO: create a file at given path with given mode
	(void)path;
	(void)mode;
	(void)fs;
	// either there is no free inode or no free blocks. since we dont have to consider
	// the situation of creating hard link.
	if (fs->sb->sb_free_inodes == 0) return -ENOSPC;
	if (fs->sb->sb_free_blocks == 0) return -ENOSPC;
	// find first unsed inode.
	uint32_t inode_idx;
	bitmap_alloc(fs->ibmap, fs->sb->sb_num_blocks, &inode_idx);
	// update inode bitmap.
	bitmap_set(fs->ibmap, fs->sb->sb_num_blocks, inode_idx, true);
	fs->sb->sb_free_inodes --;
	vsfs_inode *n_inode = &(fs->itable[inode_idx]);
	memset(n_inode, 0, sizeof(vsfs_inode));
	// update information for empty file.
	n_inode->i_mode = mode;
	n_inode->i_nlink = 1; // referenced by its partents directory (root) for file.
	n_inode->i_blocks = 0;
	n_inode->i_size = 0;
	clock_gettime(CLOCK_REALTIME, &(n_inode->i_mtime));
	
	int err = create_dentry_for_newfile(inode_idx, path);
	if (err != 0) return err;
	clock_gettime(CLOCK_REALTIME, &((&(fs->itable[VSFS_ROOT_INO]))->i_mtime) );
	return 0;
}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int vsfs_unlink(const char *path)
{
	fs_ctx *fs = get_fs();

	//TODO: remove the file at given path
	(void)path;
	(void)fs;
	// find the inode of the file.
	vsfs_dentry *dt;
	int inode_num = path_lookup(path, &dt);
	if (inode_num < 0) return -ENOENT;
	vsfs_inode* inode = &(fs->itable[inode_num]);
	// clear dentry.
	dt->ino = VSFS_INO_MAX; 
	memset(dt->name, 0, VSFS_NAME_MAX);
	// free the associated datablock and delete.
	vsfs_blk_t *indirect_ptr_lst;
	if (inode->i_blocks > VSFS_NUM_DIRECT) indirect_ptr_lst = (vsfs_blk_t *)(fs->image + VSFS_BLOCK_SIZE * (inode->i_indirect));
	for (vsfs_blk_t i = 0; i < inode->i_blocks; i ++)
	{
		if (i < VSFS_NUM_DIRECT)
		{
			bitmap_free(fs->dbmap, fs->sb->sb_num_blocks, inode->i_direct[i]);
			fs->sb->sb_free_blocks ++;
		}
		else
		{
			bitmap_free(fs->dbmap, fs->sb->sb_num_blocks, indirect_ptr_lst[i - VSFS_NUM_DIRECT]);
			fs->sb->sb_free_blocks ++;
		}
	}
	if (inode->i_blocks > VSFS_NUM_DIRECT)
	{
		bitmap_free(fs->dbmap, fs->sb->sb_num_blocks, inode->i_indirect);
		fs->sb->sb_free_blocks ++;
	}
	// clear content of inode and mark it valid on bitmap.
	memset(&(fs->itable[inode_num]), 0, sizeof(vsfs_inode));
	bitmap_free(fs->ibmap, fs->sb->sb_num_blocks, inode_num);
	fs->sb->sb_free_inodes ++;
	clock_gettime(CLOCK_REALTIME, &((&(fs->itable[VSFS_ROOT_INO]))->i_mtime));
	return 0;
}


/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *       Timestamp modifications are not recursive. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int vsfs_utimens(const char *path, const struct timespec times[2])
{
	fs_ctx *fs = get_fs();
	vsfs_inode *ino = NULL;
	
	//TODO: update the modification timestamp (mtime) in the inode for given
	// path with either the time passed as argument or the current time,
	// according to the utimensat man page
	(void)path;
	(void)fs;
	(void)ino;
	
	// 0. Check if there is actually anything to be done.
	if (times[1].tv_nsec == UTIME_OMIT) {
		// Nothing to do.
		return 0;
	}

	// 1. TODO: Find the inode for the final component in path
	vsfs_dentry *dt;
	vsfs_blk_t num;
	num = path_lookup(path, &dt);
	ino = &(fs->itable[num]);
	(void) dt;
	// 2. Update the mtime for that inode.
	//    This code is commented out to avoid failure until you have set
	//    'ino' to point to the inode structure for the inode to update.
	if (times[1].tv_nsec == UTIME_NOW) {
		if (clock_gettime(CLOCK_REALTIME, &(ino->i_mtime)) != 0) {
			// clock_gettime should not fail, unless you give it a
			// bad pointer to a timespec.
			assert(false);
		}
	} else {
		ino->i_mtime = times[1];
	}

	return 0;
}

int extending_file(off_t size, vsfs_inode *inode)
{
	fs_ctx *fs = get_fs();
	uint64_t new_blks = (uint64_t)div_round_up(size , VSFS_BLOCK_SIZE);
	uint64_t cur_blks = inode->i_blocks;

	if (new_blks - cur_blks > fs->sb->sb_free_blocks) return -ENOSPC;
	// still need to check space for indirect ptr block.
	if (cur_blks <= VSFS_NUM_DIRECT && new_blks > VSFS_NUM_DIRECT && (new_blks - cur_blks + 1 > fs->sb->sb_free_blocks)) return -ENOSPC;

	if (inode->i_size == 0) 
	{
		// if filse size is 0, we first need to allocate an new block.
		cur_blks = div_round_up(inode->i_size, VSFS_BLOCK_SIZE);
		uint32_t idx;
		bitmap_alloc(fs->dbmap, fs->sb->sb_num_blocks, &idx);
		bitmap_set(fs->dbmap, fs->sb->sb_num_blocks, idx, true);
		inode->i_direct[0] = idx;
		fs->sb->sb_free_blocks --;
		memset(fs->image + VSFS_BLOCK_SIZE * idx, 0, VSFS_BLOCK_SIZE);
		cur_blks ++;
	}
	
	uint64_t f_size = inode->i_size;
	bool indirect_ptr_flag = false;
	if (inode->i_blocks > 5) indirect_ptr_flag = true;

	for (uint64_t i = cur_blks - 1; i < new_blks; i ++)
	{
		if (i < VSFS_NUM_DIRECT)
		{
			if (i < cur_blks) // starting from the last overlapped block
			{
				int end = (f_size - 1) % VSFS_BLOCK_SIZE;
				memset(fs->image + VSFS_BLOCK_SIZE * (inode->i_direct[i]) + end + 1, 0, VSFS_BLOCK_SIZE - end - 1);
			}
			else
			{
				uint32_t idx;
				bitmap_alloc(fs->dbmap, fs->sb->sb_num_blocks, &idx);
				bitmap_set(fs->dbmap, fs->sb->sb_num_blocks, idx, true);
				inode->i_direct[i] = idx;
				fs->sb->sb_free_blocks --;
				memset(fs->image + VSFS_BLOCK_SIZE * idx, 0, VSFS_BLOCK_SIZE);
			}
		}
		else
		{	
			// check if direct ptr block is allocated.
			if (indirect_ptr_flag == false)
			{
				uint32_t ptr_dt_idx;
				bitmap_alloc(fs->dbmap, fs->sb->sb_num_blocks, &ptr_dt_idx);
				bitmap_set(fs->dbmap, fs->sb->sb_num_blocks, ptr_dt_idx, true);
				inode->i_indirect = ptr_dt_idx;
				fs->sb->sb_free_blocks --;
				indirect_ptr_flag = true;
			}

			vsfs_blk_t *indirect_ptr_lst = (vsfs_blk_t *)(fs->image + VSFS_BLOCK_SIZE * (inode->i_indirect));
			if (i < cur_blks)
			{
				int end = (f_size - 1) % VSFS_BLOCK_SIZE;
				memset(fs->image + VSFS_BLOCK_SIZE * (indirect_ptr_lst[i - VSFS_NUM_DIRECT]) + end + 1, 0, VSFS_BLOCK_SIZE - end - 1);
			}
			else
			{
				uint32_t idx;
				bitmap_alloc(fs->dbmap, fs->sb->sb_num_blocks, &idx);
				bitmap_set(fs->dbmap, fs->sb->sb_num_blocks, idx, true);
				indirect_ptr_lst[i - VSFS_NUM_DIRECT] = idx;
				fs->sb->sb_free_blocks --;
				memset(fs->image + VSFS_BLOCK_SIZE * idx, 0, VSFS_BLOCK_SIZE);
			}
		}
	}
	inode->i_blocks = new_blks;
	inode->i_size = size;
	clock_gettime(CLOCK_REALTIME, &(inode->i_mtime));
	return 0;
}

int shrinking_file(off_t size, vsfs_inode *inode)
{
	fs_ctx *fs = get_fs();
	uint64_t new_blks = (uint64_t)div_round_up(size, VSFS_BLOCK_SIZE);
	uint64_t cur_blks = inode->i_blocks;
	// if shirnk file size to zero, we need to deallocate all of its blocks.
	if (new_blks == 0)
	{
		for (uint64_t i = 0; i < cur_blks; i ++)
		{
			if (i < VSFS_NUM_DIRECT)
			{
				memset(fs->image + VSFS_BLOCK_SIZE * (inode->i_direct[i]), 0, VSFS_BLOCK_SIZE);
				bitmap_free(fs->dbmap, fs->sb->sb_num_blocks, inode->i_direct[i]);
				fs->sb->sb_free_blocks ++;
				inode->i_direct[i] = VSFS_BLK_UNASSIGNED;
			}
			else
			{
				vsfs_blk_t *indirect_ptr_lst = (vsfs_blk_t *)(fs->image + VSFS_BLOCK_SIZE * (inode->i_indirect));
				memset(fs->image + VSFS_BLOCK_SIZE * (indirect_ptr_lst[i - VSFS_NUM_DIRECT]), 0 , VSFS_BLOCK_SIZE);
				bitmap_free(fs->dbmap, fs->sb->sb_num_blocks, indirect_ptr_lst[i - VSFS_NUM_DIRECT]);
				fs->sb->sb_free_blocks ++;
				indirect_ptr_lst[i - VSFS_NUM_DIRECT] = VSFS_BLK_UNASSIGNED;
			}
		}
	}
	else
	{
		for (uint64_t i = new_blks - 1; i < cur_blks; i ++)
		{
			if (i < VSFS_NUM_DIRECT)
			{
				if (i < new_blks) // last overlapped block
				{
					int end = (size - 1) % VSFS_BLOCK_SIZE;
					memset(fs->image + VSFS_BLOCK_SIZE * (inode->i_direct[i]) + end + 1, 0, VSFS_BLOCK_SIZE - end - 1);
				}
				else
				{
					memset(fs->image + VSFS_BLOCK_SIZE * (inode->i_direct[i]), 0, VSFS_BLOCK_SIZE);
					bitmap_free(fs->dbmap, fs->sb->sb_num_blocks, inode->i_direct[i]);
					fs->sb->sb_free_blocks ++;
					inode->i_direct[i] = VSFS_BLK_UNASSIGNED;
				}
			}
			else
			{
				vsfs_blk_t *indirect_ptr_lst = (vsfs_blk_t *)(fs->image + VSFS_BLOCK_SIZE * (inode->i_indirect));
				if (i < new_blks)
				{
					int end;
					// if (size == 0)
					// {
					// 	memset(fs->image + VSFS_BLOCK_SIZE * (inode->i_direct[i]), 0, VSFS_BLOCK_SIZE);
					// 	fs->sb->sb_free_blocks ++;
					// 	continue;
					// }
					end = (size - 1) % VSFS_BLOCK_SIZE;
					memset(fs->image + VSFS_BLOCK_SIZE * (indirect_ptr_lst[i - VSFS_NUM_DIRECT]) + end + 1, 0, VSFS_BLOCK_SIZE - end - 1);
				}
				else
				{
					memset(fs->image + VSFS_BLOCK_SIZE * (indirect_ptr_lst[i - VSFS_NUM_DIRECT]), 0 , VSFS_BLOCK_SIZE);
					bitmap_free(fs->dbmap, fs->sb->sb_num_blocks, indirect_ptr_lst[i - VSFS_NUM_DIRECT]);
					fs->sb->sb_free_blocks ++;
					indirect_ptr_lst[i - VSFS_NUM_DIRECT] = VSFS_BLK_UNASSIGNED;
				}
			} 
		}
	}
	// free the indirect ptr block.
	if (new_blks <= 5 && cur_blks > 5)
	{
		memset(fs->image + VSFS_BLOCK_SIZE * (inode->i_indirect), 0, VSFS_BLOCK_SIZE);
		bitmap_free(fs->dbmap, fs->sb->sb_num_blocks, inode->i_indirect);
		inode->i_indirect = VSFS_BLK_UNASSIGNED;
		fs->sb->sb_free_blocks ++;
	}

	inode->i_blocks = new_blks;
	inode->i_size = size;
	clock_gettime(CLOCK_REALTIME, &(inode->i_mtime));
	return 0;
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   EFBIG   write would exceed the maximum file size. 
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int vsfs_truncate(const char *path, off_t size)
{
	fs_ctx *fs = get_fs();

	//TODO: set new file size, possibly "zeroing out" the uninitialized range
	(void)path;
	(void)size;
	(void)fs;
	vsfs_dentry *dt;
	int inode_num = path_lookup(path, &dt);
	if (inode_num < 0) return -ENOENT;
	(void) dt;
	vsfs_inode* inode = &(fs->itable[inode_num]);
	off_t f_size = (off_t)inode->i_size;
	// when size is equal.
	if (f_size == size) return 0;
	// when size out of max file size.
	if (size > VSFS_BLOCK_SIZE * 1029) return -EFBIG;
	int res;
	// compare firstly required blocks.
	// in the case of extending
	if (f_size > size)
	{
		res = shrinking_file(size, inode);
	}
	else // in the case of shrinking
	{
		res = extending_file(size, inode);
	}
	return res;
}


/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int vsfs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: read data from the file at given offset into the buffer
	(void)path;
	(void)buf;
	(void)size;
	(void)offset;
	(void)fs;

	vsfs_dentry *dt;
	uint32_t blk;
	int inode_num = path_lookup(path, &dt);
	vsfs_inode* inode = &(fs->itable[inode_num]);

	if (offset >= (off_t) inode->i_size) return 0;
	if (inode->i_size == 0) return 0;

	uint64_t blk_idx = offset / VSFS_BLOCK_SIZE;
	uint64_t blk_offt = offset % VSFS_BLOCK_SIZE;

	// if reaching the last block where block is not fully filled.
	if ((blk_idx + 1 == inode->i_blocks))
	{
		if (VSFS_BLOCK_SIZE - blk_offt < size) size = VSFS_BLOCK_SIZE - blk_offt;
	}
	// get the block of data.
	if (blk_idx < VSFS_NUM_DIRECT) blk = inode->i_direct[blk_idx];
	else
	{
		vsfs_blk_t *indirect_ptr_lst = (vsfs_blk_t *)(fs->image + VSFS_BLOCK_SIZE * (inode->i_indirect));
		blk = indirect_ptr_lst[blk_idx - VSFS_NUM_DIRECT];
	} 
	char *head = fs->image + VSFS_BLOCK_SIZE * blk + blk_offt;
	memcpy(buf, head, size);
	return size;
}
         
/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   EFBIG   write would exceed the maximum file size 
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int vsfs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: write data from the buffer into the file at given offset, possibly
	// "zeroing out" the uninitialized range
	(void)path;
	(void)buf;
	(void)size;
	(void)offset;
	(void)fs;

	vsfs_dentry *dt;
	uint32_t blk;
	int inode_num = path_lookup(path, &dt);
	vsfs_inode* inode = &(fs->itable[inode_num]);
	// if oversize, need to extend the file.
	if (size + offset > inode->i_size)
	{
		int err = vsfs_truncate(path, size + offset);
		if (err < 0) return err;
	}
	clock_gettime(CLOCK_REALTIME, &(inode->i_mtime));
	uint64_t blk_idx = offset / VSFS_BLOCK_SIZE;
	uint64_t blk_offt = offset % VSFS_BLOCK_SIZE;
	// get data block
	if (blk_idx < VSFS_NUM_DIRECT) blk = inode->i_direct[blk_idx];
	else
	{
		vsfs_blk_t *indirect_ptr_lst = (vsfs_blk_t *)(fs->image + VSFS_BLOCK_SIZE * (inode->i_indirect));
		blk = indirect_ptr_lst[blk_idx - VSFS_NUM_DIRECT];
	}
	char *head = fs->image + VSFS_BLOCK_SIZE * blk + blk_offt;
	memcpy(head, buf, size);
	return size;
}


static struct fuse_operations vsfs_ops = {
	.destroy  = vsfs_destroy,
	.statfs   = vsfs_statfs,
	.getattr  = vsfs_getattr,
	.readdir  = vsfs_readdir,
	.create   = vsfs_create,
	.unlink   = vsfs_unlink,
	.utimens  = vsfs_utimens,
	.truncate = vsfs_truncate,
	.read     = vsfs_read,
	.write    = vsfs_write,
};

int main(int argc, char *argv[])
{
	vsfs_opts opts = {0};// defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!vsfs_opt_parse(&args, &opts)) return 1;

	fs_ctx fs = {0};
	if (!vsfs_init(&fs, &opts)) {
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}

	return fuse_main(args.argc, args.argv, &vsfs_ops, &fs);
}
