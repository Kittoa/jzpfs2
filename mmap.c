/*
 * 内存映射
 */

#include "jzpfs.h"

static int jzpfs_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{	
	printk(KERN_ALERT "jzpfs_fault");
	int err;
	struct file *file, *lower_file;
	const struct vm_operations_struct *lower_vm_ops;
	struct vm_area_struct lower_vma;

	memcpy(&lower_vma, vma, sizeof(struct vm_area_struct));
	file = lower_vma.vm_file;
	lower_vm_ops = JZPFS_F(file)->lower_vm_ops;
	BUG_ON(!lower_vm_ops);

	lower_file = jzpfs_lower_file(file);
	/*
	 * XXX: vm_ops->fault may be called in parallel.  Because we have to
	 * resort to temporarily changing the vma->vm_file to point to the
	 * lower file, a concurrent invocation of jzpfs_fault could see a
	 * different value.  In this workaround, we keep a different copy of
	 * the vma structure in our stack, so we never expose a different
	 * value of the vma->vm_file called to us, even temporarily.  A
	 * better fix would be to change the calling semantics of ->fault to
	 * take an explicit file pointer.
	 */
	lower_vma.vm_file = lower_file;
	err = lower_vm_ops->fault(&lower_vma, vmf);
	return err;
}

static int jzpfs_page_mkwrite(struct vm_area_struct *vma,
			       struct vm_fault *vmf)
{
	printk(KERN_ALERT "jzpfs_page_mkwrite");
	int err = 0;
	struct file *file, *lower_file;
	const struct vm_operations_struct *lower_vm_ops;
	struct vm_area_struct lower_vma;

	memcpy(&lower_vma, vma, sizeof(struct vm_area_struct));
	file = lower_vma.vm_file;
	lower_vm_ops = JZPFS_F(file)->lower_vm_ops;
	BUG_ON(!lower_vm_ops);
	if (!lower_vm_ops->page_mkwrite)
		goto out;

	lower_file = jzpfs_lower_file(file);
	/*
	 * XXX: vm_ops->page_mkwrite may be called in parallel.
	 * Because we have to resort to temporarily changing the
	 * vma->vm_file to point to the lower file, a concurrent
	 * invocation of jzpfs_page_mkwrite could see a different
	 * value.  In this workaround, we keep a different copy of the
	 * vma structure in our stack, so we never expose a different
	 * value of the vma->vm_file called to us, even temporarily.
	 * A better fix would be to change the calling semantics of
	 * ->page_mkwrite to take an explicit file pointer.
	 */
	lower_vma.vm_file = lower_file;
	err = lower_vm_ops->page_mkwrite(&lower_vma, vmf);
out:
	return err;
}

static ssize_t jzpfs_direct_IO(struct kiocb *iocb,
				struct iov_iter *iter, loff_t pos)
{
	printk(KERN_ALERT "jzpfs_direct_IO");
	/*
	 * This function should never be called directly.  We need it
	 * to exist, to get past a check in open_check_o_direct(),
	 * which is called from do_last().
	 */
	return -EINVAL;
}

const struct address_space_operations jzpfs_aops = {
	.direct_IO = jzpfs_direct_IO,
};

const struct vm_operations_struct jzpfs_vm_ops = {
	.fault		= jzpfs_fault,
	.page_mkwrite	= jzpfs_page_mkwrite,
};
