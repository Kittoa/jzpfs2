/*
 *dentry的操作
 */

#include "jzpfs.h"

/* The dentry cache is just so we have properly sized dentries */
static struct kmem_cache *jzpfs_dentry_cachep;

int jzpfs_init_dentry_cache(void)
{	
	printk(KERN_ALERT "jzpfs_init_dentry_cache");
	jzpfs_dentry_cachep =
		kmem_cache_create("jzpfs_dentry",
				  sizeof(struct jzpfs_dentry_info),
				  0, SLAB_RECLAIM_ACCOUNT, NULL);

	return jzpfs_dentry_cachep ? 0 : -ENOMEM;
}

void jzpfs_destroy_dentry_cache(void)
{
	printk(KERN_ALERT "jzpfs_destroy_dentry_cache");
	if (jzpfs_dentry_cachep)
		kmem_cache_destroy(jzpfs_dentry_cachep);
}

void free_dentry_private_data(struct dentry *dentry)
{
	printk(KERN_ALERT "jzpfs_free_dentry_private_data");
	if (!dentry || !dentry->d_fsdata)
		return;
	kmem_cache_free(jzpfs_dentry_cachep, dentry->d_fsdata);
	dentry->d_fsdata = NULL;
}

/* allocate new dentry private data */
int new_dentry_private_data(struct dentry *dentry)
{
	printk(KERN_ALERT "jzpfs_new_dentry_private_data");
	struct jzpfs_dentry_info *info = JZPFS_D(dentry);

	/* use zalloc to init dentry_info.lower_path */
	info = kmem_cache_zalloc(jzpfs_dentry_cachep, GFP_ATOMIC);
	if (!info)
		return -ENOMEM;

	spin_lock_init(&info->lock);
	dentry->d_fsdata = info;

	return 0;
}

static int jzpfs_inode_test(struct inode *inode, void *candidate_lower_inode)
{
	printk(KERN_ALERT "jzpfs_inode_test");
	struct inode *current_lower_inode = jzpfs_lower_inode(inode);
	if (current_lower_inode == (struct inode *)candidate_lower_inode)
		return 1; /* found a match */
	else
		return 0; /* no match */
}

static int jzpfs_inode_set(struct inode *inode, void *lower_inode)
{
	printk(KERN_ALERT "jzpfs_inode_set");
	/* we do actual inode initialization in jzpfs_iget */
	return 0;
}

/*
 * 分配我们新的inode
 */
struct inode *jzpfs_iget(struct super_block *sb, struct inode *lower_inode)
{
	printk(KERN_ALERT "jzpfs_iget");
	struct jzpfs_inode_info *info;
	struct inode *inode; /* the new inode to return */
	int err;

	inode = iget5_locked(sb, /* our superblock */
			     /*
			      * hashval: we use inode number, but we can
			      * also use "(unsigned long)lower_inode"
			      * instead.
			      */
			     lower_inode->i_ino, /* hashval */
			     jzpfs_inode_test,	/* inode comparison function */
			     jzpfs_inode_set, /* inode init function */
			     lower_inode); /* data passed to test+set fxns */
	if (!inode) {
		err = -EACCES;
		iput(lower_inode);
		return ERR_PTR(err);
	}
	/* if found a cached inode, then just return it */
	if (!(inode->i_state & I_NEW))
		return inode;

	/* initialize new inode */
	info = JZPFS_I(inode);

	inode->i_ino = lower_inode->i_ino;
	if (!igrab(lower_inode)) {
		err = -ESTALE;
		return ERR_PTR(err);
	}
	jzpfs_set_lower_inode(inode, lower_inode);

	inode->i_version++;

	/* use different set of inode ops for symlinks & directories */
	if (S_ISDIR(lower_inode->i_mode))
		inode->i_op = &jzpfs_dir_iops;
	else if (S_ISLNK(lower_inode->i_mode))
		inode->i_op = &jzpfs_symlink_iops;
	else
		inode->i_op = &jzpfs_main_iops;

	/* use different set of file ops for directories */
	if (S_ISDIR(lower_inode->i_mode))
		inode->i_fop = &jzpfs_dir_fops;
	else
		inode->i_fop = &jzpfs_main_fops;

	inode->i_mapping->a_ops = &jzpfs_aops;

	inode->i_atime.tv_sec = 0;
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_sec = 0;
	inode->i_mtime.tv_nsec = 0;
	inode->i_ctime.tv_sec = 0;
	inode->i_ctime.tv_nsec = 0;

	/* properly initialize special inodes */
	if (S_ISBLK(lower_inode->i_mode) || S_ISCHR(lower_inode->i_mode) ||
	    S_ISFIFO(lower_inode->i_mode) || S_ISSOCK(lower_inode->i_mode))
		init_special_inode(inode, lower_inode->i_mode,
				   lower_inode->i_rdev);

	/* all well, copy inode attributes */
	fsstack_copy_attr_all(inode, lower_inode);
	fsstack_copy_inode_size(inode, lower_inode);

	unlock_new_inode(inode);
	return inode;
}

/*
 *连接一个jzpfs inode dentry / inode与 lower的inode dentry
 */
int jzpfs_interpose(struct dentry *dentry, struct super_block *sb,
		     struct path *lower_path)
{
	printk(KERN_ALERT "jzpfs_interpose");
	int err = 0;
	struct inode *inode;
	struct inode *lower_inode;
	struct super_block *lower_sb;

	lower_inode = d_inode(lower_path->dentry);
	lower_sb = jzpfs_lower_super(sb);

	/* 检查lower的文件系统是否没有穿过安装点 */
	if (lower_inode->i_sb != lower_sb) {
		err = -EXDEV;
		goto out;
	}

	/*
	 * 我们通过调用jzpfs_iget来分配我们新的inode，它将初始化一些新的inode的字段
	 */

	/* inherit lower inode number for jzpfs's inode */
	inode = jzpfs_iget(sb, lower_inode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}

	d_add(dentry, inode);

out:
	return err;
}

/*
 * 查找路径
 */
static struct dentry *__jzpfs_lookup(struct dentry *dentry,
				      unsigned int flags,
				      struct path *lower_parent_path)
{
	int err = 0;
	struct vfsmount *lower_dir_mnt;
	struct dentry *lower_dir_dentry = NULL;
	struct dentry *lower_dentry;
	const char *name;
	struct path lower_path;
	struct qstr this;

	/* must initialize dentry operations */
	d_set_d_op(dentry, &jzpfs_dops);

	if (IS_ROOT(dentry))
		goto out;

	name = dentry->d_name.name;

	/* now start the actual lookup procedure */
	lower_dir_dentry = lower_parent_path->dentry;
	lower_dir_mnt = lower_parent_path->mnt;

	/* Use vfs_path_lookup to check if the dentry exists or not */
	err = vfs_path_lookup(lower_dir_dentry, lower_dir_mnt, name, 0,
			      &lower_path);

	/* no error: handle positive dentries */
	if (!err) {
		jzpfs_set_lower_path(dentry, &lower_path);
		err = jzpfs_interpose(dentry, dentry->d_sb, &lower_path);
		if (err) /* path_put underlying path on error */
			jzpfs_put_reset_lower_path(dentry);
		goto out;
	}

	/*
	 * We don't consider ENOENT an error, and we want to return a
	 * negative dentry.
	 */
	if (err && err != -ENOENT)
		goto out;

	/* instatiate a new negative dentry */
	this.name = name;
	this.len = strlen(name);
	this.hash = full_name_hash(lower_dir_dentry, this.name, this.len);
	lower_dentry = d_lookup(lower_dir_dentry, &this);
	if (lower_dentry)
		goto setup_lower;

	lower_dentry = d_alloc(lower_dir_dentry, &this);
	if (!lower_dentry) {
		err = -ENOMEM;
		goto out;
	}
	d_add(lower_dentry, NULL); /* instantiate and hash */

setup_lower:
	lower_path.dentry = lower_dentry;
	lower_path.mnt = mntget(lower_dir_mnt);
	jzpfs_set_lower_path(dentry, &lower_path);

	/*
	 * If the intent is to create a file, then don't return an error, so
	 * the VFS will continue the process of making this negative dentry
	 * into a positive one.
	 */
	if (flags & (LOOKUP_CREATE|LOOKUP_RENAME_TARGET))
		err = 0;

out:
	return ERR_PTR(err);
}


/*
 * 查找路径
 */
struct dentry *jzpfs_lookup(struct inode *dir, struct dentry *dentry,
			     unsigned int flags)
{
	printk(KERN_ALERT "jzpfs_lookup");
	int err;
	struct dentry *ret, *parent;
	struct path lower_parent_path;

	parent = dget_parent(dentry);

	jzpfs_get_lower_path(parent, &lower_parent_path);

	/* allocate dentry private data.  We free it in ->d_release */
	err = new_dentry_private_data(dentry);
	if (err) {
		ret = ERR_PTR(err);
		goto out;
	}
	ret = __jzpfs_lookup(dentry, flags, &lower_parent_path);
	if (IS_ERR(ret))
		goto out;
	if (ret)
		dentry = ret;
	if (d_inode(dentry))
		fsstack_copy_attr_times(d_inode(dentry),
					jzpfs_lower_inode(d_inode(dentry)));
	/* update parent directory's atime */
	fsstack_copy_attr_atime(d_inode(parent),
				jzpfs_lower_inode(d_inode(parent)));

out:
	jzpfs_put_lower_path(parent, &lower_parent_path);
	dput(parent);
	return ret;
}
