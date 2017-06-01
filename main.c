/*
 *将各个钩子函数挂好
 */

#include "jzpfs.h"
#include <linux/module.h>

/*
 * 读超级块信息
 */
static int jzpfs_read_super(struct super_block *sb, void *raw_data, int silent)
{	
	printk(KERN_ALERT "jzpfs_read_super");
	int err = 0;
	struct super_block *lower_sb;
	struct path lower_path;
	char *dev_name = (char *) raw_data;
	struct inode *inode;

	if (!dev_name) {
		printk(KERN_ERR
		       "jzpfs: read_super: missing dev_name argument\n");
		err = -EINVAL;
		goto out;
	}

	/* 解析下层路径 */
	err = kern_path(dev_name, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
			&lower_path);
	if (err) {
		printk(KERN_ERR	"jzpfs: error accessing "
		       "lower directory '%s'\n", dev_name);
		goto out;
	}

	/* 分配超级块private数据 */
	sb->s_fs_info = kzalloc(sizeof(struct jzpfs_sb_info), GFP_KERNEL);
	if (!JZPFS_SB(sb)) {
		printk(KERN_CRIT "jzpfs: read_super: out of memory\n");
		err = -ENOMEM;
		goto out_free;
	}

	/* 把上层的超级块信息赋给下层数据块 */
	lower_sb = lower_path.dentry->d_sb;
	atomic_inc(&lower_sb->s_active);
	jzpfs_set_lower_super(sb, lower_sb);

	/* 从下层文件系统继承maxbytes */
	sb->s_maxbytes = lower_sb->s_maxbytes;

	/*
	 * 时间粒度为1
	 */
	sb->s_time_gran = 1;

	sb->s_op = &jzpfs_sops;

	/* 的到一个新的inode，分配我们自己的根目录项 */
	inode = jzpfs_iget(sb, d_inode(lower_path.dentry));
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_sput;
	}
	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		err = -ENOMEM;
		goto out_iput;
	}
	d_set_d_op(sb->s_root, &jzpfs_dops);

	/* 连接上下的dentry */
	sb->s_root->d_fsdata = NULL;
	err = new_dentry_private_data(sb->s_root);
	if (err)
		goto out_freeroot;

	/* 如果到这里都没有错误 */

	/* 把下层的denty 放到jzpfs的根目录下 */
	jzpfs_set_lower_path(sb->s_root, &lower_path);

	
	d_rehash(sb->s_root);
	if (!silent)
		printk(KERN_INFO
		       "jzpfs: mounted on top of %s type %s\n",
		       dev_name, lower_sb->s_type->name);
	goto out; 
	
out_freeroot:
	dput(sb->s_root);
out_iput:
	iput(inode);
out_sput:
	
	atomic_dec(&lower_sb->s_active);
	kfree(JZPFS_SB(sb));
	sb->s_fs_info = NULL;
out_free:
	path_put(&lower_path);

out:
	return err;
}

/*
 *mount我们的文件系统
 */
struct dentry *jzpfs_mount(struct file_system_type *fs_type, int flags,
			    const char *dev_name, void *raw_data)
{
	printk(KERN_ALERT "jzpfs_mount");
	void *lower_path_name = (void *) dev_name;

	return mount_nodev(fs_type, flags, lower_path_name,
			   jzpfs_read_super);
}

static struct file_system_type jzpfs_fs_type = {
	.owner		= THIS_MODULE,
	.name			= JZPFS_NAME,
	.mount		= jzpfs_mount,
	.kill_sb		= generic_shutdown_super,
	.fs_flags		= 0,
};
MODULE_ALIAS_FS(JZPFS_NAME);

static int __init init_jzpfs_fs(void)
{
	int err;

	pr_info("Registering jzpfs " JZPFS_VERSION "\n");

	err = jzpfs_init_inode_cache();
	if (err)
		goto out;
	err = jzpfs_init_dentry_cache();
	if (err)
		goto out;
	err = register_filesystem(&jzpfs_fs_type);
out:
	if (err) {
		jzpfs_destroy_inode_cache();
		jzpfs_destroy_dentry_cache();
	}
	return err;
}

static void __exit exit_jzpfs_fs(void)
{
	jzpfs_destroy_inode_cache();
	jzpfs_destroy_dentry_cache();
	unregister_filesystem(&jzpfs_fs_type);
	pr_info("Completed jzpfs module unload\n");
}

MODULE_AUTHOR("jzp");
MODULE_DESCRIPTION("jzfs " JZPFS_VERSION);
MODULE_LICENSE("GPL");

module_init(init_jzpfs_fs);
module_exit(exit_jzpfs_fs);
