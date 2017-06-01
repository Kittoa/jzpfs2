/*
 * 文件操作
 */

#include "jzpfs.h"

/*
 *读文件
 */
static ssize_t jzpfs_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{	
	printk(KERN_ALERT "jzpfs_read");
	int err;
	size_t i=0;

	struct file *lower_file;
	struct dentry *dentry = file->f_path.dentry;

	lower_file = jzpfs_lower_file(file);
//	printk(KERN_ALERT "read-f_flags:%d\n", lower_file->f_flags);
	
	
	err = vfs_read(lower_file, buf, count, ppos);
	
	//将字母都小写
	if(lower_file->f_flags == 00000615){
		printk(KERN_ALERT "解密");
		while(count != 4096 && i<=count){
			if(buf[i]>='A' && buf[i]<='Z')
				buf[i] += 32;
				i++;
		}
	}	
	
	/* update our inode atime upon a successful lower read */
	if (err >= 0)
		fsstack_copy_attr_atime(d_inode(dentry),
					file_inode(lower_file));

	return err;
}

/*
 * 写文件
 */
static ssize_t jzpfs_write(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{	
	printk(KERN_ALERT "jzpfs_write");
	int err;
	size_t i=0;

	struct file *lower_file;
	struct dentry *dentry = file->f_path.dentry;

	lower_file = jzpfs_lower_file(file);
//	printk(KERN_ALERT "write-f_flags:%d\n", lower_file->f_flags);

	//将字母都大写
	if(lower_file->f_flags == 00000615){
		printk(KERN_ALERT "加密");
		while(count != 4096 && i<=count){
			if(buf[i]>='a' && buf[i]<='z')
				buf[i] -= 32;
				i++;
		}
	}
	
//	printk(KERN_ALERT "0flags:%d\n", file->f_flags);
//	printk(KERN_ALERT "1buf:%s\n", buf);
//	printk(KERN_ALERT "2count:%d\n", count);
//	printk(KERN_ALERT "3ppos:%d\n", *ppos);

	
	err = vfs_write(lower_file, buf, count, ppos);
	/* update our inode times+sizes upon a successful lower write */
	if (err >= 0) {
		fsstack_copy_inode_size(d_inode(dentry),
					file_inode(lower_file));
		fsstack_copy_attr_times(d_inode(dentry),
					file_inode(lower_file));
	}

	return err;
}

/*
 * 读文件目录
 */
static int jzpfs_readdir(struct file *file, struct dir_context *ctx)
{	
	printk(KERN_ALERT "jzpfs_readdir");
	int err;
	struct file *lower_file = NULL;
	struct dentry *dentry = file->f_path.dentry;

	lower_file = jzpfs_lower_file(file);
	err = iterate_dir(lower_file, ctx);
	file->f_pos = lower_file->f_pos;
	if (err >= 0)		/* copy the atime */
		fsstack_copy_attr_atime(d_inode(dentry),
					file_inode(lower_file));
	return err;
}

static long jzpfs_unlocked_ioctl(struct file *file, unsigned int cmd,
				  unsigned long arg)
{	
	printk(KERN_ALERT "jzpfs_unlocked_icotl");
	long err = -ENOTTY;
	struct file *lower_file;

	lower_file = jzpfs_lower_file(file);

	/* XXX: use vfs_ioctl if/when VFS exports it */
	if (!lower_file || !lower_file->f_op)
		goto out;
	if (lower_file->f_op->unlocked_ioctl)
		err = lower_file->f_op->unlocked_ioctl(lower_file, cmd, arg);

	/* some ioctls can change inode attributes (EXT2_IOC_SETFLAGS) */
	if (!err)
		fsstack_copy_attr_all(file_inode(file),
				      file_inode(lower_file));
out:
	return err;
}

#ifdef CONFIG_COMPAT
static long jzpfs_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	printk(KERN_ALERT "jzpfs_compat_ioctl");
	long err = -ENOTTY;
	struct file *lower_file;

	lower_file = jzpfs_lower_file(file);

	/* XXX: use vfs_ioctl if/when VFS exports it */
	if (!lower_file || !lower_file->f_op)
		goto out;
	if (lower_file->f_op->compat_ioctl)
		err = lower_file->f_op->compat_ioctl(lower_file, cmd, arg);

out:
	return err;
}
#endif

/*
 * 内存映射
 */
static int jzpfs_mmap(struct file *file, struct vm_area_struct *vma)
{	
	printk(KERN_ALERT "jzpfs_mmap");
	int err = 0;
	bool willwrite;
	struct file *lower_file;
	const struct vm_operations_struct *saved_vm_ops = NULL;

	/* 这可能推迟到mmap的写页面 */
	willwrite = ((vma->vm_flags | VM_SHARED | VM_WRITE) == vma->vm_flags);

	/*
	 * F检查较低文件系统是否支持 - > writepage
	 */
	lower_file = jzpfs_lower_file(file);
	if (willwrite && !lower_file->f_mapping->a_ops->writepage) {
		err = -EINVAL;
		printk(KERN_ERR "jzpfs: lower file system does not "
		       "support writeable mmap\n");
		goto out;
	}

	
	if (!JZPFS_F(file)->lower_vm_ops) {
		err = lower_file->f_op->mmap(lower_file, vma);
		if (err) {
			printk(KERN_ERR "jzpfs: lower mmap failed %d\n", err);
			goto out;
		}
		saved_vm_ops = vma->vm_ops; /* save: came from lower ->mmap */
	}

	
	file_accessed(file);
	vma->vm_ops = &jzpfs_vm_ops;

	file->f_mapping->a_ops = &jzpfs_aops; /* set our aops */
	if (!JZPFS_F(file)->lower_vm_ops) /* save for our ->fault */
		JZPFS_F(file)->lower_vm_ops = saved_vm_ops;

out:
	return err;
}

/*
 * 打开一个文件
 */
static int jzpfs_open(struct inode *inode, struct file *file) //163842
{	
	printk(KERN_ALERT "jzpfs_open");
	int err = 0, errr=0;
	char buff[3];
	char buf[3]={'J','F','S'};
	loff_t pos;
	struct file *lower_file = NULL;
	struct path lower_path;

	/* don't open unhashed/deleted files */
	if (d_unhashed(file->f_path.dentry)) {
		err = -ENOENT;
		goto out_err;
	}

	file->private_data =
		kzalloc(sizeof(struct jzpfs_file_info), GFP_KERNEL);
	if (!JZPFS_F(file)) {
		err = -ENOMEM;
		goto out_err;
	}	

	/* open lower object and link jzpfs's file struct to lower's */
	jzpfs_get_lower_path(file->f_path.dentry, &lower_path);
	lower_file = dentry_open(&lower_path, file->f_flags, current_cred());
	path_put(&lower_path);
	if (IS_ERR(lower_file)) {
		err = PTR_ERR(lower_file);
		lower_file = jzpfs_lower_file(file);
		if (lower_file) {
			jzpfs_set_lower_file(file, NULL);
			fput(lower_file); /* fput calls dput for lower_dentry */
		}
	} else {
		jzpfs_set_lower_file(file, lower_file);
	}

	if (err)
		kfree(JZPFS_F(file));
	else
		fsstack_copy_attr_all(inode, jzpfs_lower_inode(inode));
	
/*	if(lower_file->f_pos == 0){
		printk(KERN_ALERT "open-f_pos1:%lld\n", lower_file->f_pos);
		//mm_segment_t old_fs;
		//old_fs = get_fs();
		//set_fs(KERNEL_DS);
		errr = vfs_write(lower_file, "JFS", 3, 0);
		//set_fs(old_fs);
		printk(KERN_ALERT "errr:%d\n", errr);
		file->f_flags = 00000615;
		printk(KERN_ALERT "open-f_flags:%s\n", file->f_flags);
		generic_file_llseek(lower_file, 3, SEEK_SET);
		generic_file_llseek(file, 3, SEEK_SET);
		printk(KERN_ALERT "open-f_pos2:%lld\n", lower_file->f_pos);
	}else{
		errr = vfs_read(lower_file, buff, 3, 0);
		printk(KERN_ALERT "open-buff:%s\n", buff);
		if(buff[0]=='J' && buff[1]=='F' && buff[2]=='S'){
			file->f_flags = 00000615;
			printk(KERN_ALERT "open-f_flags:%s\n", file->f_flags);		
		}
	}
*/		
	if(file->f_flags & O_CREAT){
//		printk(KERN_ALERT "open-f_flags:%d\n", file->f_flags);
//		printk(KERN_ALERT "open-o_creat:%s\n", O_CREAT);
//		printk(KERN_ALERT "create--------");
		pos = lower_file->f_pos;
		mm_segment_t old_fs;
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		printk(KERN_ALERT "open-f_pos1:%lld\n", pos);
		errr = vfs_write(lower_file, buf, 3, &pos);
		printk(KERN_ALERT "open-f_pos2:%lld\n", pos);
		set_fs(old_fs);
		lower_file->f_pos = pos;
		file->f_pos = pos;
		printk(KERN_ALERT "open-errr:%d\n", errr);
		if (errr >= 0)
		fsstack_copy_attr_atime(d_inode(lower_file->f_path.dentry),
					file_inode(lower_file));
		lower_file->f_flags = 00000615;
//		printk(KERN_ALERT "open-f_flags:%d\n", file->f_flags);			
		goto out_err;
	}	

	mm_segment_t old_fs;
	old_fs = get_fs();
	errr = vfs_read(lower_file, buff, 3, 0);
	set_fs(old_fs);
	if (errr >= 0)
		fsstack_copy_attr_atime(d_inode(lower_file->f_path.dentry),
					file_inode(lower_file));

	if(buff[3]=='J' && buff[4]=='F' && buff[5]=='S'){
		lower_file->f_flags = 00000615;
		file->f_pos=3;
//		printk(KERN_ALERT "buf open-f_flags:%d\n", file->f_flags);		
	}
	
	
out_err:
	return err;
}

static int jzpfs_flush(struct file *file, fl_owner_t id)
{	
	printk(KERN_ALERT "jzpfs_flush");
	int err = 0;
	struct file *lower_file = NULL;

	lower_file = jzpfs_lower_file(file);
	if (lower_file && lower_file->f_op && lower_file->f_op->flush) {
		filemap_write_and_wait(file->f_mapping);
		err = lower_file->f_op->flush(lower_file, id);
	}

	return err;
}

/* 释放所有lower对象引用并释放文件信息结构  */
static int jzpfs_file_release(struct inode *inode, struct file *file)
{
	printk(KERN_ALERT "jzpfs_file_release");
	struct file *lower_file;

	lower_file = jzpfs_lower_file(file);
	if (lower_file) {
		jzpfs_set_lower_file(file, NULL);
		fput(lower_file);
	}

	kfree(JZPFS_F(file));
	return 0;
}

/*
 * 从文件系统缓存写入磁盘
 */
static int jzpfs_fsync(struct file *file, loff_t start, loff_t end,
			int datasync)
{	
	printk(KERN_ALERT "jzpfs_fsync");
	int err;
	struct file *lower_file;
	struct path lower_path;
	struct dentry *dentry = file->f_path.dentry;

	err = __generic_file_fsync(file, start, end, datasync);
	if (err)
		goto out;
	lower_file = jzpfs_lower_file(file);
	jzpfs_get_lower_path(dentry, &lower_path);
	err = vfs_fsync_range(lower_file, start, end, datasync);
	jzpfs_put_lower_path(dentry, &lower_path);
out:
	return err;
}

static int jzpfs_fasync(int fd, struct file *file, int flag)
{	
	printk(KERN_ALERT "jzpfs_fasync");
	int err = 0;
	struct file *lower_file = NULL;

	lower_file = jzpfs_lower_file(file);
	if (lower_file->f_op && lower_file->f_op->fasync)
		err = lower_file->f_op->fasync(fd, lower_file, flag);

	return err;
}

/* jzpfs不能使用generic_file_llseek作为 - > llseek，因为它只会设置上部文件的偏移量。 
  *所以我们必须实现自己的方法来一致地设置上下文件偏移量。
 */
static loff_t jzpfs_file_llseek(struct file *file, loff_t offset, int whence)
{
	printk(KERN_ALERT "jzpfs_file_llseek");
	int err;
	struct file *lower_file;

	err = generic_file_llseek(file, offset, whence);
	if (err < 0)
		goto out;

	lower_file = jzpfs_lower_file(file);
	err = generic_file_llseek(lower_file, offset, whence);

out:
	return err;
}

/*
 * jzpfs read_iter, redirect modified iocb to lower read_iter
 */
ssize_t jzpfs_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	printk(KERN_ALERT "jzpfs_read_iter");
	int err;
	struct file *file = iocb->ki_filp, *lower_file;

	lower_file = jzpfs_lower_file(file);
	if (!lower_file->f_op->read_iter) {
		err = -EINVAL;
		goto out;
	}

	get_file(lower_file); /* prevent lower_file from being released */
	iocb->ki_filp = lower_file;
	err = lower_file->f_op->read_iter(iocb, iter);
	iocb->ki_filp = file;
	fput(lower_file);
	/* update upper inode atime as needed */
	if (err >= 0 || err == -EIOCBQUEUED)
		fsstack_copy_attr_atime(d_inode(file->f_path.dentry),
					file_inode(lower_file));
out:
	return err;
}

/*
 * jzpfs write_iter, redirect modified iocb to lower write_iter
 */
ssize_t jzpfs_write_iter(struct kiocb *iocb, struct iov_iter *iter)
{	
	printk(KERN_ALERT "jzpfs_write_iter");
	int err;
	struct file *file = iocb->ki_filp, *lower_file;

	lower_file = jzpfs_lower_file(file);
	if (!lower_file->f_op->write_iter) {
		err = -EINVAL;
		goto out;
	}

	get_file(lower_file); /* prevent lower_file from being released */
	iocb->ki_filp = lower_file;
	err = lower_file->f_op->write_iter(iocb, iter);
	iocb->ki_filp = file;
	fput(lower_file);
	/* update upper inode times/sizes as needed */
	if (err >= 0 || err == -EIOCBQUEUED) {
		fsstack_copy_inode_size(d_inode(file->f_path.dentry),
					file_inode(lower_file));
		fsstack_copy_attr_times(d_inode(file->f_path.dentry),
					file_inode(lower_file));
	}
out:
	return err;
}

const struct file_operations jzpfs_main_fops = {
	.llseek		= generic_file_llseek,
	.read			= jzpfs_read,
	.write		= jzpfs_write,
	.unlocked_ioctl	= jzpfs_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= jzpfs_compat_ioctl,
#endif
	.mmap			= jzpfs_mmap,
	.open			= jzpfs_open,
	.flush		= jzpfs_flush,
	.release		= jzpfs_file_release,
	.fsync		= jzpfs_fsync,
	.fasync		= jzpfs_fasync,
	.read_iter		= jzpfs_read_iter,
	.write_iter		= jzpfs_write_iter,
};

const struct file_operations jzpfs_dir_fops = {
	.llseek		= jzpfs_file_llseek,
	.read			= generic_read_dir,
	.iterate		= jzpfs_readdir,
	.unlocked_ioctl	= jzpfs_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= jzpfs_compat_ioctl,
#endif
	.open			= jzpfs_open,
	.release		= jzpfs_file_release,
	.flush		= jzpfs_flush,
	.fsync		= jzpfs_fsync,
	.fasync		= jzpfs_fasync,
};
