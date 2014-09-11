#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Incarnation P. Lee <incarnation.p.lee@gmail.com>");

#define KNOTE_MAJOR            311
#define KNOTE_MINOR            0
#define KNOTE_NAME             "knote"
#define KNOTE_DEV_COUNT        16
#define KNOTE_SET_SIZE         4096

struct knote_set_list {
	char *base;
	struct knote_set_list *next;
};

struct knote_dev {
	int set_size;
	int last_len;
	struct knote_set_list *data;
	struct cdev cdev;
};

static struct knote_dev *kn_dev;

int knote_open(struct inode *inode, struct file *filp)
{
	int retval;
	struct knote_dev *dev;

	retval = 0;
	dev = container_of(inode->i_cdev, struct knote_dev, cdev);
	if (filp) {
		//if (filp->private_data == dev && (filp->f_mode & FMODE_WRITE))
		//	retval = -ETXTBSY;
		filp->private_data = dev;
		filp->f_pos = 0;
	}

	pr_info("Info: Device operation: [ [32mOpen[0m ]\n");
	return retval;
}

static void knote_truncate(struct knote_dev *dev)
{
	struct knote_set_list *node, *tmp;

	if (dev) {
		node = dev->data;
		while (tmp = node->next, tmp) {
			kfree(node);
			node = tmp;
		}
	}
}

int knote_release(struct inode *inode, struct file *filp)
{
	struct knote_dev *dev;

	dev = container_of(inode->i_cdev, struct knote_dev, cdev);
	if (filp && dev) {
		filp->private_data = NULL;
		knote_truncate(dev);
	}

	pr_info("Info: Device operation: [ [32mRelease[0m ]\n");
	return 0;
}

static struct knote_set_list *
knote_set_access_by_index(struct knote_set_list *node, int index)
{
	struct knote_set_list *result;

	result = node;
	if (node && index > 0)
		while(index-- && result)
			result = result->next;

	return result;
}

ssize_t knote_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	ssize_t retval;
	int index, rest, set_size, last_len;
	loff_t cnt;
	struct knote_set_list *tmp;

	retval = 0;
	cnt = 0;

	if (filp && buf && f_pos && kn_dev) {
		set_size = kn_dev->set_size;
		last_len = kn_dev->last_len;
		index = *f_pos / set_size;
		rest = *f_pos % set_size;

		tmp = knote_set_access_by_index(kn_dev->data, index);
		if (tmp) {
			if (count > set_size - rest)
				count = set_size - rest;

			/* handle last node of knote set list */
			if (!tmp->next) {
				if (last_len < rest)
					count = 0;
				else
					count = last_len - rest;
			}

			if (count && copy_to_user(buf, tmp->base, count)) {
				retval = -EFAULT;
				goto FAIL;
			}

			*f_pos += count;
			retval = count;
			pr_info("Info: Dev OPT: [ [32mRead[0m ] %d\n",
				(int)count);
		}

	}

FAIL:
	return retval;
}

static const struct file_operations knote_fops = {
	.owner = THIS_MODULE,
	.open  = knote_open,
	.release = knote_release,
	.read  = knote_read,
	//.write = knote_write,
};

static void knote_destroy(struct knote_dev *dev)
{
	struct knote_set_list *tmp;
	struct knote_set_list *node;

	if (dev) {
		node = dev->data;
		while (node) {
			tmp = node;
			kfree(tmp->base); /* free data memory */
			kfree(tmp);       /* free list node   */
			node = node->next;
		}
		kfree(dev);               /* free knote dev   */
	}
}

static int knote_allocate(struct knote_dev *dev)
{
	int retval;

	retval = 0;
	dev->data = kmalloc(sizeof(struct knote_set_list), GFP_KERNEL);
	if (!dev->data) {
		retval = -ENOMEM;
		goto FAIL;
	}

	dev->data->next = NULL;
	dev->data->base = kmalloc(KNOTE_SET_SIZE, GFP_KERNEL);
	if (!dev->data->base) {
		retval = -ENOMEM;
		kfree(dev->data);
	}

	dev->set_size = KNOTE_SET_SIZE;
FAIL:
	return retval;
}

static int knote_setup(void)
{
	int retval;

	retval = 0;
	kn_dev = kmalloc(sizeof(struct knote_dev), GFP_KERNEL);
	if (!kn_dev) {
		retval = -ENOMEM;
		goto FAIL;
	}

	kn_dev->last_len = 0;
	retval = knote_allocate(kn_dev);

FAIL:
	return retval;
}

static int knote_register(struct knote_dev *dev)
{
	int retval, devno;

	devno = MKDEV(KNOTE_MAJOR, KNOTE_MINOR);
	retval = register_chrdev_region(devno, KNOTE_DEV_COUNT, KNOTE_NAME);
	if (retval) {
		pr_err("Erro: register_chrdev_region %s\n", KNOTE_NAME);
		goto FAIL;
	}

	dev->cdev.owner = THIS_MODULE;
	cdev_init(&dev->cdev, &knote_fops);
	retval = cdev_add(&dev->cdev, devno, KNOTE_DEV_COUNT);

FAIL:
	return retval;
}

static int __init knote_init(void)
{
	int retval;

	retval = knote_setup();
	if (retval)
		goto FAIL;

	retval = knote_register(kn_dev);
	if (retval) {
		knote_destroy(kn_dev);
		goto FAIL;
	}
	pr_info("Info: Knote [32mEnabled[0m\n");
FAIL:
	return retval;
}

static void knote_unregister(struct knote_dev *dev)
{
	dev_t devno;

	if (dev) {
		cdev_del(&dev->cdev);
		devno = MKDEV(KNOTE_MAJOR, KNOTE_MINOR);
		unregister_chrdev_region(devno, KNOTE_DEV_COUNT);
	}
}

static void __exit knote_exit(void)
{
	knote_unregister(kn_dev);
	knote_destroy(kn_dev);
	kn_dev = NULL;
	pr_info("Info: Knote [31mDisabled[0m\n");
}

module_init(knote_init);
module_exit(knote_exit);
