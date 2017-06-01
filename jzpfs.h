/*
 *jzpfs数据定义
 */

#ifndef _JZPFS_H_
#define _JZPFS_H_

#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/seq_file.h>
#include <linux/statfs.h>
#include <linux/fs_stack.h>
#include <linux/magic.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/xattr.h>

/* 文件系统名 */
#define JZPFS_NAME "jzpfs"

/* jzpfs root inode number */
#define JZPFS_ROOT_INO     1

/* DEBUG信息 */
#define UDBG printk(KERN_DEFAULT "DBG:%s:%s:%d\n", __FILE__, __func__, __LINE__)

/* 特定文件中定义的操作数组，及钩子函数 */
extern const struct file_operations jzpfs_main_fops;
extern const struct file_operations jzpfs_dir_fops;
extern const struct inode_operations jzpfs_main_iops;
extern const struct inode_operations jzpfs_dir_iops;
extern const struct inode_operations jzpfs_symlink_iops;
extern const struct super_operations jzpfs_sops;
extern const struct dentry_operations jzpfs_dops;
extern const struct address_space_operations jzpfs_aops, jzpfs_dummy_aops;
extern const struct vm_operations_struct jzpfs_vm_ops;

extern int jzpfs_init_inode_cache(void);
extern void jzpfs_destroy_inode_cache(void);
extern int jzpfs_init_dentry_cache(void);
extern void jzpfs_destroy_dentry_cache(void);
extern int new_dentry_private_data(struct dentry *dentry);
extern void free_dentry_private_data(struct dentry *dentry);
//查找路径
extern struct dentry *jzpfs_lookup(struct inode *dir, struct dentry *dentry,
				    unsigned int flags);
//初始化inode
extern struct inode *jzpfs_iget(struct super_block *sb,
				 struct inode *lower_inode);
extern int jzpfs_interpose(struct dentry *dentry, struct super_block *sb,
			    struct path *lower_path);

/* file private data */
struct jzpfs_file_info {
	struct file *lower_file;
	const struct vm_operations_struct *lower_vm_ops;
};

/* jzpfs inode data in memory */
struct jzpfs_inode_info {
	struct inode *lower_inode;
	struct inode vfs_inode;
};

/* jzpfs dentry data in memory */
struct jzpfs_dentry_info {
	spinlock_t lock;	/* protects lower_path */
	struct path lower_path;
};

/* jzpfs super-block data in memory */
struct jzpfs_sb_info {
	struct super_block *lower_sb;
};

/*
 * inode to private data
 *
 * 由于我们使用的容器和struct inode是jzpfs_inode_info结构，jzpfs_i永远（一个非空节点的指针）
 * 返回一个有效的非空指针。
 */
static inline struct jzpfs_inode_info *JZPFS_I(const struct inode *inode)
{
	return container_of(inode, struct jzpfs_inode_info, vfs_inode);
}

/* dentry to private data */
#define JZPFS_D(dent) ((struct jzpfs_dentry_info *)(dent)->d_fsdata)

/* superblock to private data */
#define JZPFS_SB(super) ((struct jzpfs_sb_info *)(super)->s_fs_info)

/* file to private Data */
#define JZPFS_F(file) ((struct jzpfs_file_info *)((file)->private_data))

/* file to lower file */
static inline struct file *jzpfs_lower_file(const struct file *f)
{
	return JZPFS_F(f)->lower_file;
}

static inline void jzpfs_set_lower_file(struct file *f, struct file *val)
{
	JZPFS_F(f)->lower_file = val;
}

/* inode to lower inode. */
static inline struct inode *jzpfs_lower_inode(const struct inode *i)
{
	return JZPFS_I(i)->lower_inode;
}

static inline void jzpfs_set_lower_inode(struct inode *i, struct inode *val)
{
	JZPFS_I(i)->lower_inode = val;
}

/* superblock to lower superblock */
static inline struct super_block *jzpfs_lower_super(
	const struct super_block *sb)
{
	return JZPFS_SB(sb)->lower_sb;
}

static inline void jzpfs_set_lower_super(struct super_block *sb,
					  struct super_block *val)
{
	JZPFS_SB(sb)->lower_sb = val;
}

/* path based (dentry/mnt) macros */
static inline void pathcpy(struct path *dst, const struct path *src)
{
	dst->dentry = src->dentry;
	dst->mnt = src->mnt;
}
/* Returns struct path.  Caller must path_put it. */
static inline void jzpfs_get_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	spin_lock(&JZPFS_D(dent)->lock);
	pathcpy(lower_path, &JZPFS_D(dent)->lower_path);
	path_get(lower_path);
	spin_unlock(&JZPFS_D(dent)->lock);
	return;
}
static inline void jzpfs_put_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	path_put(lower_path);
	return;
}
static inline void jzpfs_set_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	spin_lock(&JZPFS_D(dent)->lock);
	pathcpy(&JZPFS_D(dent)->lower_path, lower_path);
	spin_unlock(&JZPFS_D(dent)->lock);
	return;
}
static inline void jzpfs_reset_lower_path(const struct dentry *dent)
{
	spin_lock(&JZPFS_D(dent)->lock);
	JZPFS_D(dent)->lower_path.dentry = NULL;
	JZPFS_D(dent)->lower_path.mnt = NULL;
	spin_unlock(&JZPFS_D(dent)->lock);
	return;
}
static inline void jzpfs_put_reset_lower_path(const struct dentry *dent)
{
	struct path lower_path;
	spin_lock(&JZPFS_D(dent)->lock);
	pathcpy(&lower_path, &JZPFS_D(dent)->lower_path);
	JZPFS_D(dent)->lower_path.dentry = NULL;
	JZPFS_D(dent)->lower_path.mnt = NULL;
	spin_unlock(&JZPFS_D(dent)->lock);
	path_put(&lower_path);
	return;
}

/* locking helpers */
static inline struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *dir = dget_parent(dentry);
	inode_lock_nested(d_inode(dir), I_MUTEX_PARENT);
	return dir;
}

static inline void unlock_dir(struct dentry *dir)
{
	inode_unlock(d_inode(dir));
	dput(dir);
}
#endif	/* not _JZPFS_H_ */
