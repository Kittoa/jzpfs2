/*
 * inode操作
 */

#include "jzpfs.h"

/*
 *创建inode
 */
static int jzpfs_create(struct inode *dir, struct dentry *dentry,
			 umode_t mode, bool want_excl)
{	
	printk(KERN_ALERT "jzpfs_create_inode");
	int err;
	struct dentry *lower_dentry;
	struct dentry *lower_parent_dentry = NULL;
	struct path lower_path;

	jzpfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_parent_dentry = lock_parent(lower_dentry);

	err = vfs_create(d_inode(lower_parent_dentry), lower_dentry, mode,
			 want_excl);
	if (err)
		goto out;
	err = jzpfs_interpose(dentry, dir->i_sb, &lower_path);
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, jzpfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));

out:
	unlock_dir(lower_parent_dentry);
	jzpfs_put_lower_path(dentry, &lower_path);
	return err;
}

/*
 *硬链接
 */
static int jzpfs_link(struct dentry *old_dentry, struct inode *dir,
		       struct dentry *new_dentry)
{	
	printk(KERN_ALERT "jzpfs_link");
	struct dentry *lower_old_dentry;
	struct dentry *lower_new_dentry;
	struct dentry *lower_dir_dentry;
	u64 file_size_save;
	int err;
	struct path lower_old_path, lower_new_path;

	file_size_save = i_size_read(d_inode(old_dentry));
	jzpfs_get_lower_path(old_dentry, &lower_old_path);
	jzpfs_get_lower_path(new_dentry, &lower_new_path);
	lower_old_dentry = lower_old_path.dentry;
	lower_new_dentry = lower_new_path.dentry;
	lower_dir_dentry = lock_parent(lower_new_dentry);

	err = vfs_link(lower_old_dentry, d_inode(lower_dir_dentry),
		       lower_new_dentry, NULL);
	if (err || !d_inode(lower_new_dentry))
		goto out;

	err = jzpfs_interpose(new_dentry, dir->i_sb, &lower_new_path);
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, d_inode(lower_new_dentry));
	fsstack_copy_inode_size(dir, d_inode(lower_new_dentry));
	set_nlink(d_inode(old_dentry),
		  jzpfs_lower_inode(d_inode(old_dentry))->i_nlink);
	i_size_write(d_inode(new_dentry), file_size_save);
out:
	unlock_dir(lower_dir_dentry);
	jzpfs_put_lower_path(old_dentry, &lower_old_path);
	jzpfs_put_lower_path(new_dentry, &lower_new_path);
	return err;
}

/*
 * 不创建VFS对象
 */
static int jzpfs_unlink(struct inode *dir, struct dentry *dentry)
{
	printk(KERN_ALERT "jzpfs_unlink");
	int err;
	struct dentry *lower_dentry;
	struct inode *lower_dir_inode = jzpfs_lower_inode(dir);
	struct dentry *lower_dir_dentry;
	struct path lower_path;

	jzpfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	dget(lower_dentry);
	lower_dir_dentry = lock_parent(lower_dentry);

	err = vfs_unlink(lower_dir_inode, lower_dentry, NULL);

	/*
	 * 在NFS之上取消链接可能导致silly-renamed重命名的文件。 尝试删除这样的文件会导致下面的NFS中的EBUSY。
         * 稍后，NFS将删除silly-renamed名称的文件，所以我们只需要在这里检测它们，并处理这样的EBUSY错误，
         * 就像上面的文件被成功删除一样。
	 */
	if (err == -EBUSY && lower_dentry->d_flags & DCACHE_NFSFS_RENAMED)
		err = 0;
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, lower_dir_inode);
	fsstack_copy_inode_size(dir, lower_dir_inode);
	set_nlink(d_inode(dentry),
		  jzpfs_lower_inode(d_inode(dentry))->i_nlink);
	d_inode(dentry)->i_ctime = dir->i_ctime;
	d_drop(dentry); 
out:
	unlock_dir(lower_dir_dentry);
	dput(lower_dentry);
	jzpfs_put_lower_path(dentry, &lower_path);
	return err;
}

/*
 *符号链接
 */
static int jzpfs_symlink(struct inode *dir, struct dentry *dentry,
			  const char *symname)
{	
	printk(KERN_ALERT "jzpfs_symlink");
	int err;
	struct dentry *lower_dentry;
	struct dentry *lower_parent_dentry = NULL;
	struct path lower_path;

	jzpfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_parent_dentry = lock_parent(lower_dentry);

	err = vfs_symlink(d_inode(lower_parent_dentry), lower_dentry, symname);
	if (err)
		goto out;
	err = jzpfs_interpose(dentry, dir->i_sb, &lower_path);
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, jzpfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));

out:
	unlock_dir(lower_parent_dentry);
	jzpfs_put_lower_path(dentry, &lower_path);
	return err;
}

/*
 *创建目录
 */
static int jzpfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	printk(KERN_ALERT "jzpfs_mkdir");
	int err;
	struct dentry *lower_dentry;
	struct dentry *lower_parent_dentry = NULL;
	struct path lower_path;

	jzpfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_parent_dentry = lock_parent(lower_dentry);

	err = vfs_mkdir(d_inode(lower_parent_dentry), lower_dentry, mode);
	if (err)
		goto out;

	err = jzpfs_interpose(dentry, dir->i_sb, &lower_path);
	if (err)
		goto out;

	fsstack_copy_attr_times(dir, jzpfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));
	/* update number of links on parent directory */
	set_nlink(dir, jzpfs_lower_inode(dir)->i_nlink);

out:
	unlock_dir(lower_parent_dentry);
	jzpfs_put_lower_path(dentry, &lower_path);
	return err;
}

/*
 * 删除目录
 */
static int jzpfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	printk(KERN_ALERT "jzpfs_rmdir");
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;
	int err;
	struct path lower_path;

	jzpfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_dir_dentry = lock_parent(lower_dentry);

	err = vfs_rmdir(d_inode(lower_dir_dentry), lower_dentry);
	if (err)
		goto out;

	d_drop(dentry);	/* drop our dentry on success (why not VFS's job?) */
	if (d_inode(dentry))
		clear_nlink(d_inode(dentry));
	fsstack_copy_attr_times(dir, d_inode(lower_dir_dentry));
	fsstack_copy_inode_size(dir, d_inode(lower_dir_dentry));
	set_nlink(dir, d_inode(lower_dir_dentry)->i_nlink);

out:
	unlock_dir(lower_dir_dentry);
	jzpfs_put_lower_path(dentry, &lower_path);
	return err;
}

/*
 * 创建特殊文件
 */
static int jzpfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode,
			dev_t dev)
{	
	printk(KERN_ALERT "jzpfs_mknod");
	int err;
	struct dentry *lower_dentry;
	struct dentry *lower_parent_dentry = NULL;
	struct path lower_path;

	jzpfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_parent_dentry = lock_parent(lower_dentry);

	err = vfs_mknod(d_inode(lower_parent_dentry), lower_dentry, mode, dev);
	if (err)
		goto out;

	err = jzpfs_interpose(dentry, dir->i_sb, &lower_path);
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, jzpfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));

out:
	unlock_dir(lower_parent_dentry);
	jzpfs_put_lower_path(dentry, &lower_path);
	return err;
}

/*
 * 文件重命名
 */
static int jzpfs_rename(struct inode *old_dir, struct dentry *old_dentry,
			 struct inode *new_dir, struct dentry *new_dentry)
{
	printk(KERN_ALERT "jzpfs_rename");
	int err = 0;
	struct dentry *lower_old_dentry = NULL;
	struct dentry *lower_new_dentry = NULL;
	struct dentry *lower_old_dir_dentry = NULL;
	struct dentry *lower_new_dir_dentry = NULL;
	struct dentry *trap = NULL;
	struct path lower_old_path, lower_new_path;

	jzpfs_get_lower_path(old_dentry, &lower_old_path);
	jzpfs_get_lower_path(new_dentry, &lower_new_path);
	lower_old_dentry = lower_old_path.dentry;
	lower_new_dentry = lower_new_path.dentry;
	lower_old_dir_dentry = dget_parent(lower_old_dentry);
	lower_new_dir_dentry = dget_parent(lower_new_dentry);

	trap = lock_rename(lower_old_dir_dentry, lower_new_dir_dentry);
	/* source should not be ancestor of target */
	if (trap == lower_old_dentry) {
		err = -EINVAL;
		goto out;
	}
	/* target should not be ancestor of source */
	if (trap == lower_new_dentry) {
		err = -ENOTEMPTY;
		goto out;
	}

	err = vfs_rename(d_inode(lower_old_dir_dentry), lower_old_dentry,
			 d_inode(lower_new_dir_dentry), lower_new_dentry,
			 NULL, 0);
	if (err)
		goto out;

	fsstack_copy_attr_all(new_dir, d_inode(lower_new_dir_dentry));
	fsstack_copy_inode_size(new_dir, d_inode(lower_new_dir_dentry));
	if (new_dir != old_dir) {
		fsstack_copy_attr_all(old_dir,
				      d_inode(lower_old_dir_dentry));
		fsstack_copy_inode_size(old_dir,
					d_inode(lower_old_dir_dentry));
	}

out:
	unlock_rename(lower_old_dir_dentry, lower_new_dir_dentry);
	dput(lower_old_dir_dentry);
	dput(lower_new_dir_dentry);
	jzpfs_put_lower_path(old_dentry, &lower_old_path);
	jzpfs_put_lower_path(new_dentry, &lower_new_path);
	return err;
}

static int jzpfs_readlink(struct dentry *dentry, char __user *buf, int bufsiz)
{
	printk(KERN_ALERT "jzpfs_readlink");
	int err;
	struct dentry *lower_dentry;
	struct path lower_path;

	jzpfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!d_inode(lower_dentry)->i_op ||
	    !d_inode(lower_dentry)->i_op->readlink) {
		err = -EINVAL;
		goto out;
	}

	err = d_inode(lower_dentry)->i_op->readlink(lower_dentry,
						    buf, bufsiz);
	if (err < 0)
		goto out;
	fsstack_copy_attr_atime(d_inode(dentry), d_inode(lower_dentry));

out:
	jzpfs_put_lower_path(dentry, &lower_path);
	return err;
}

static const char *jzpfs_follow_link(struct dentry *dentry, void **cookie)
{
	printk(KERN_ALERT "jzpfs_follow_link");
	char *buf;
	int len = PAGE_SIZE, err;
	mm_segment_t old_fs;

	/* This is freed by the put_link method assuming a successful call. */
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) {
		buf = ERR_PTR(-ENOMEM);
		return buf;
	}

	/* read the symlink, and then we will follow it */
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = jzpfs_readlink(dentry, buf, len);
	set_fs(old_fs);
	if (err < 0) {
		kfree(buf);
		buf = ERR_PTR(err);
	} else {
		buf[err] = '\0';
	}
	return *cookie = buf;
}

/*
 * 文件权限
 */
static int jzpfs_permission(struct inode *inode, int mask)
{
	printk(KERN_ALERT "jzpfs_permission");
	struct inode *lower_inode;
	int err;

	lower_inode = jzpfs_lower_inode(inode);
	err = inode_permission(lower_inode, mask);
	return err;
}

/*
 * 设置inode属性
 */
static int jzpfs_setattr(struct dentry *dentry, struct iattr *ia)
{
	printk(KERN_ALERT "jzpfs_setattr");
	int err;
	struct dentry *lower_dentry;
	struct inode *inode;
	struct inode *lower_inode;
	struct path lower_path;
	struct iattr lower_ia;

	inode = d_inode(dentry);

	/*
	 * 检查用户是否有权限更改inode。
	 */
	err = setattr_prepare(dentry, ia);
	if (err)
		goto out_err;

	jzpfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_inode = jzpfs_lower_inode(inode);

	/* 准备我们自己的下层结构iattr（lower_ia） */
	memcpy(&lower_ia, ia, sizeof(lower_ia));
	if (ia->ia_valid & ATTR_FILE)
		lower_ia.ia_file = jzpfs_lower_file(ia->ia_file);

	
	if (ia->ia_valid & ATTR_SIZE) {
		err = inode_newsize_ok(inode, ia->ia_size);
		if (err)
			goto out;
		truncate_setsize(inode, ia->ia_size);
	}

	/*
	 * 模式更改用于清除setuid / setgid位。 允许较低的fs以其自己的方式解释。
	 */
	if (lower_ia.ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		lower_ia.ia_valid &= ~ATTR_MODE;

	
	inode_lock(d_inode(lower_dentry));
	err = notify_change(lower_dentry, &lower_ia, 
			    NULL);
	inode_unlock(d_inode(lower_dentry));
	if (err)
		goto out;

	
	fsstack_copy_attr_all(inode, lower_inode);
	

out:
	jzpfs_put_lower_path(dentry, &lower_path);
out_err:
	return err;
}

/*
 * 获取inode属性
 */
static int jzpfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
			  struct kstat *stat)
{
	printk(KERN_ALERT "jzpfs_getattr");
	int err;
	struct kstat lower_stat;
	struct path lower_path;

	jzpfs_get_lower_path(dentry, &lower_path);
	err = vfs_getattr(&lower_path, &lower_stat);
	if (err)
		goto out;
	fsstack_copy_attr_all(d_inode(dentry),
			      d_inode(lower_path.dentry));
	generic_fillattr(d_inode(dentry), stat);
	stat->blocks = lower_stat.blocks;
out:
	jzpfs_put_lower_path(dentry, &lower_path);
	return err;
}

static int jzpfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		size_t size, int flags)
{
	printk(KERN_ALERT "jzpfs_setxattr");
	int err; struct dentry *lower_dentry;
	struct path lower_path;

	jzpfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!(d_inode(lower_dentry)->i_opflags & IOP_XATTR)) {
		err = -EOPNOTSUPP;
		goto out;
	}
	err = vfs_setxattr(lower_dentry, name, value, size, flags);
	if (err)
		goto out;
	fsstack_copy_attr_all(d_inode(dentry),
			      d_inode(lower_path.dentry));
out:
	jzpfs_put_lower_path(dentry, &lower_path);
	return err;
}

static ssize_t jzpfs_getxattr(struct dentry *dentry, const char *name, void *buffer,
		size_t size)
{
	printk(KERN_ALERT "jzpfs_getxattr");
	int err;
	struct dentry *lower_dentry;
	struct path lower_path;

	jzpfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!(d_inode(lower_dentry)->i_opflags & IOP_XATTR)) {
		err = -EOPNOTSUPP;
		goto out;
	}
	err = vfs_getxattr(lower_dentry, name, buffer, size);
	if (err)
		goto out;
	fsstack_copy_attr_atime(d_inode(dentry),
				d_inode(lower_path.dentry));
out:
	jzpfs_put_lower_path(dentry, &lower_path);
	return err;
}

static ssize_t jzpfs_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	printk(KERN_ALERT "jzpfs_listxattr");
	int err;
	struct dentry *lower_dentry;
	struct path lower_path;

	jzpfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!d_inode(lower_dentry)->i_op->listxattr) {
		err = -EOPNOTSUPP;
		goto out;
	}
	err = vfs_listxattr(lower_dentry, buffer, buffer_size);
	if (err)
		goto out;
	fsstack_copy_attr_atime(d_inode(dentry),
				d_inode(lower_path.dentry));
out:
	jzpfs_put_lower_path(dentry, &lower_path);
	return err;
}

static int jzpfs_removexattr(struct dentry *dentry, struct inode *inode, const char *name)
{
	printk(KERN_ALERT "jzpfs_removexattr");
	int err;
	struct dentry *lower_dentry;
	struct inode *lower_inode;
	struct path lower_path;

	jzpfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_inode = jzpfs_lower_inode(inode);
	if (!(lower_inode->i_opflags & IOP_XATTR)) {
		err = -EOPNOTSUPP;
		goto out;
	}
	err = vfs_removexattr(lower_dentry, name);
	if (err)
		goto out;
	fsstack_copy_attr_all(d_inode(dentry), lower_inode);
out:
	jzpfs_put_lower_path(dentry, &lower_path);
	return err;
}

static const char *jzpfs_get_link(struct dentry *dentry, struct inode *inode,
				   struct delayed_call *done)
{
	printk(KERN_ALERT "jzpfs_get_link");
	char *buf;
	int len = PAGE_SIZE, err;
	mm_segment_t old_fs;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) {
		buf = ERR_PTR(-ENOMEM);
		return buf;
	}

	
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = jzpfs_readlink(dentry, buf, len);
	set_fs(old_fs);
	if (err < 0) {
		kfree(buf);
		buf = ERR_PTR(err);
	} else {
		buf[err] = '\0';
	}
	set_delayed_call(done, kfree_link, buf);
	return buf;
}

const struct inode_operations jzpfs_symlink_iops = {
	.readlink	= jzpfs_readlink,
	.permission	= jzpfs_permission,
	.setattr	= jzpfs_setattr,
	.getattr	= jzpfs_getattr,
	.get_link	= jzpfs_get_link,
	.listxattr	= jzpfs_listxattr,
};

const struct inode_operations jzpfs_dir_iops = {
	.create		= jzpfs_create,
	.lookup		= jzpfs_lookup,
	.link		= jzpfs_link,
	.unlink		= jzpfs_unlink,
	.symlink	= jzpfs_symlink,
	.mkdir		= jzpfs_mkdir,
	.rmdir		= jzpfs_rmdir,
	.mknod		= jzpfs_mknod,
	.rename		= jzpfs_rename,
	.permission	= jzpfs_permission,
	.setattr	= jzpfs_setattr,
	.getattr	= jzpfs_getattr,
	.listxattr	= jzpfs_listxattr,
};

const struct inode_operations jzpfs_main_iops = {
	.permission	= jzpfs_permission,
	.setattr	= jzpfs_setattr,
	.getattr	= jzpfs_getattr,
	.listxattr	= jzpfs_listxattr,
};
