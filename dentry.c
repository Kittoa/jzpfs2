/*
 * dentry操作
 */

#include "jzpfs.h"

/*
 *  检测目录是否有效
 * returns: -ERRNO if error (returned to user)
 *          0: tell VFS to invalidate dentry 无效目录
 *          1: dentry is valid 目录有效
 */
static int jzpfs_d_revalidate(struct dentry *dentry, unsigned int flags)
{	
	printk(KERN_ALERT "jzpfs_d_revalidate");
	struct path lower_path;
	struct dentry *lower_dentry;
	int err = 1;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	jzpfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!(lower_dentry->d_flags & DCACHE_OP_REVALIDATE))
		goto out;
	err = lower_dentry->d_op->d_revalidate(lower_dentry, flags);
out:
	jzpfs_put_lower_path(dentry, &lower_path);
	return err;
}

/*
 *释放目录
 */
static void jzpfs_d_release(struct dentry *dentry)
{	
	printk(KERN_ALERT "jzpfs_d_release");
	/* release and reset the lower paths */
	jzpfs_put_reset_lower_path(dentry);
	free_dentry_private_data(dentry);
	return;
}

const struct dentry_operations jzpfs_dops = {
	.d_revalidate	= jzpfs_d_revalidate,
	.d_release	= jzpfs_d_release,
};
