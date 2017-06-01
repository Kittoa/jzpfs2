/*
 * super操作
 */

#include "jzpfs.h"

/*
 * The inode cache is used with alloc_inode for both our inode info and the
 * vfs inode.
 */
static struct kmem_cache *jzpfs_inode_cachep;

/* 卸载文件系统最后的操作 */
static void jzpfs_put_super(struct super_block *sb)
{	
	printk(KERN_ALERT "jzpfs_put_super");
	struct jzpfs_sb_info *spd;
	struct super_block *s;

	spd = JZPFS_SB(sb);
	if (!spd)
		return;

	/* decrement lower super references */
	s = jzpfs_lower_super(sb);
	jzpfs_set_lower_super(sb, NULL);
	atomic_dec(&s->s_active);

	kfree(spd);
	sb->s_fs_info = NULL;
}

/*
 *获取文件系统的信息
 */
static int jzpfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	printk(KERN_ALERT "jzpfs_statfs");
	int err;
	struct path lower_path;

	jzpfs_get_lower_path(dentry, &lower_path);
	err = vfs_statfs(&lower_path, buf);
	jzpfs_put_lower_path(dentry, &lower_path);

	/* 设置jzpfs的魔数 */
	buf->f_type = JZPFS_SUPER_MAGIC;

	return err;
}

/*
 * 再次挂在文件系统
 * @flags: numeric mount options
 * @options: mount options string
 */
static int jzpfs_remount_fs(struct super_block *sb, int *flags, char *options)
{
	printk(KERN_ALERT "jzpfs_remount_fs");
	int err = 0;

	/*
	 * The VFS will take care of "ro" and "rw" flags among others.  We
	 * can safely accept a few flags (RDONLY, MANDLOCK), and honor
	 * SILENT, but anything else left over is an error.
	 */
	if ((*flags & ~(MS_RDONLY | MS_MANDLOCK | MS_SILENT)) != 0) {
		printk(KERN_ERR
		       "jzpfs: remount flags 0x%x unsupported\n", *flags);
		err = -EINVAL;
	}

	return err;
}

/*
 * 引用计数法销毁inode
 */
static void jzpfs_evict_inode(struct inode *inode)
{
	printk(KERN_ALERT "jzpfs_evict_inode");
	struct inode *lower_inode;

	truncate_inode_pages(&inode->i_data, 0);
	clear_inode(inode);
	/*
	 * 减少对lower_inode的引用，当初始创建它时，它被read_inode增加。
	 */
	lower_inode = jzpfs_lower_inode(inode);
	jzpfs_set_lower_inode(inode, NULL);
	iput(lower_inode);
}

/*
 *申请inode
 */
static struct inode *jzpfs_alloc_inode(struct super_block *sb)
{
	printk(KERN_ALERT "jzpfs_alloc_inode");
	struct jzpfs_inode_info *i;

	i = kmem_cache_alloc(jzpfs_inode_cachep, GFP_KERNEL);
	if (!i)
		return NULL;

	/* 将所有的内容记录到inode 0 */
	memset(i, 0, offsetof(struct jzpfs_inode_info, vfs_inode));

	i->vfs_inode.i_version = 1;
	return &i->vfs_inode;
}

/*
 *销毁inode
 */
static void jzpfs_destroy_inode(struct inode *inode)
{
	printk(KERN_ALERT "jzpfs_destroy_inode");
	kmem_cache_free(jzpfs_inode_cachep, JZPFS_I(inode));
}

/* jzpfs inode cache constructor */
static void init_once(void *obj)
{
//	printk(KERN_ALERT "jzpfs_init_once");
	struct jzpfs_inode_info *i = obj;

	inode_init_once(&i->vfs_inode);
}

/*
 *inode缓存
 */
int jzpfs_init_inode_cache(void)
{
	printk(KERN_ALERT "jzpfs_init_inode_cache");
	int err = 0;

	jzpfs_inode_cachep =
		kmem_cache_create("jzpfs_inode_cache",
				  sizeof(struct jzpfs_inode_info), 0,
				  SLAB_RECLAIM_ACCOUNT, init_once);
	if (!jzpfs_inode_cachep)
		err = -ENOMEM;
	return err;
}

/* jzpfs inode cache destructor */
void jzpfs_destroy_inode_cache(void)
{
	printk(KERN_ALERT "jzpfs_destroy_inode_cache");
	if (jzpfs_inode_cachep)
		kmem_cache_destroy(jzpfs_inode_cachep);
}

/*
 * 仅在nfs中使用，可以杀死任何待处理的RPC任务，以便后续代码实际上可以成功，不会丢弃需要处理的任务。
 */
static void jzpfs_umount_begin(struct super_block *sb)
{
	printk(KERN_ALERT "jzpfs_umount_begin");
	struct super_block *lower_sb;

	lower_sb = jzpfs_lower_super(sb);
	if (lower_sb && lower_sb->s_op && lower_sb->s_op->umount_begin)
		lower_sb->s_op->umount_begin(lower_sb);
}

const struct super_operations jzpfs_sops = {
	.put_super	= jzpfs_put_super,
	.statfs		= jzpfs_statfs,
	.remount_fs	= jzpfs_remount_fs,
	.evict_inode	= jzpfs_evict_inode,
	.umount_begin	= jzpfs_umount_begin,
	.show_options	= generic_show_options,
	.alloc_inode	= jzpfs_alloc_inode,
	.destroy_inode	= jzpfs_destroy_inode,
	.drop_inode	= generic_delete_inode,
};
