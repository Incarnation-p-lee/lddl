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
#if 1
	#define KNOTE_DEBUG
#endif

#ifndef KNOTE_DEBUG
	#undef pr_info
	#define pr_info(fmt, ...)
#endif

struct knote_set_list {
	char *base;
	struct knote_set_list *next;
};

struct knote_dev {
	int set_size;
	int tail_rest;
	struct knote_set_list *data;
	struct knote_set_list *tail; /* last node of set list */
	struct cdev cdev;
};

static struct knote_dev *kn_dev;

static int knote_file_length(struct knote_dev *dev)
{
	int retval;
	int cnt;
	struct knote_set_list *iter;

	retval = 0;
	cnt = 0;
	if (dev) {
		iter = dev->data;
		while (iter != dev->tail) {
			cnt++;
			iter = iter->next;
		}
		retval = cnt * KNOTE_SET_SIZE + dev->tail_rest;
	}

	return retval;
}

int knote_open(struct inode *inode, struct file *filp)
{
	int retval;
	struct knote_dev *dev;

	retval = 0;
	dev = container_of(inode->i_cdev, struct knote_dev, cdev);
	if (filp) {
                filp->private_data = dev;
		if ((O_RDONLY == (filp->f_flags & O_ACCMODE)) ||
			(O_RDWR == (filp->f_flags & O_ACCMODE))) {
			/* read-only or read-write */
			filp->f_pos = 0;
		} else if (O_APPEND & filp->f_flags) {
			/* write-only with append */
			filp->f_pos = knote_file_length(dev);
		} else {
			/* write-only override */
			dev->tail = dev->data;
			dev->tail_rest = 0;
			filp->f_pos = 0;
		}
	}

	pr_info("Info: Device operation: [ [32mOpen[0m ]\n");
	return retval;
}

static void knote_truncate(struct knote_dev *dev)
{
	struct knote_set_list *node, *next;

	if (dev) {
		next = dev->tail->next;
		while (node = next, node) {
			next = node->next;
			kfree(node->base);
			kfree(node);
			pr_info("Kfree one node of knote set list.\n");
		}
		dev->tail->next = NULL;
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

static struct knote_set_list *
knote_set_append_node(struct knote_set_list *node)
{
	struct knote_set_list *result;

	result = node;
	if (node) {
		while(result->next)
			result = result->next;

		result->next = kmalloc(sizeof(*result), GFP_KERNEL);
		if (!result->next) {
			result = NULL;
			goto FAIL;
		}
		result = result->next;
		result->next = NULL;

		result->base = kmalloc(KNOTE_SET_SIZE, GFP_KERNEL);
		if (!result->base) {
			kfree(result);
			goto FAIL;
		}
		pr_info("Kmalloc one node of knote set list.\n");
	}
FAIL:
	return result;
}

ssize_t knote_write(struct file *filp, const char __user *buf,
	size_t count, loff_t *f_pos)
{
	ssize_t retval;
	int index, rest, set_size;
	struct knote_set_list *tmp;
	struct knote_dev *dev;

	retval = 0;
	dev = kn_dev;

	if (filp && buf && f_pos && dev) {
		set_size = dev->set_size;
		index = *f_pos / set_size;
		rest = *f_pos % set_size;

		tmp = knote_set_access_by_index(dev->data, index);
		if (!tmp)
			tmp = knote_set_append_node(dev->data);

		if (count > set_size - rest)
			count = set_size - rest;

		if (copy_from_user(tmp->base + rest, buf, count)) {
			retval = -EFAULT;
			goto FAIL;
		}

		*f_pos += count;
		retval = count;
		dev->tail = tmp;
		dev->tail_rest = count + rest;
		pr_info("Info: Dev OPT: [ [33mWrite[0m ] %d\n", (int)count);
	}

FAIL:
	return retval;
}

static int
knote_set_is_over_tail(struct knote_dev *dev, struct knote_set_list *node)
{
	int retval;

	retval = 1;
	if (dev && node) {
		while (node) {
			if (dev->tail == node) {
				retval = 0;
				break;
			}
			node = node->next;
		}
	}

	return retval;
}

ssize_t knote_read(struct file *filp, char __user *buf,
	size_t count, loff_t *f_pos)
{
	ssize_t retval;
	int index, rest, set_size, tail_rest;
	struct knote_set_list *tmp;
	struct knote_dev *dev;

	retval = 0;
	dev = kn_dev;

	if (filp && buf && f_pos && dev) {
		set_size = dev->set_size;
		tail_rest = dev->tail_rest;
		index = *f_pos / set_size;
		rest = *f_pos % set_size;

		tmp = knote_set_access_by_index(dev->data, index);
		if (tmp && !knote_set_is_over_tail(dev, tmp)) {
			/* handle last node of knote set list */
			if (!tmp->next || tmp == dev->tail) {
				if (tail_rest < rest)
					count = 0;
				else if (count > tail_rest - rest)
					count = tail_rest - rest;
			} else if (count > set_size - rest)
				count = set_size - rest;

			if (copy_to_user(buf, tmp->base + rest, count)) {
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
	.owner   = THIS_MODULE,
	.open    = knote_open,
	.release = knote_release,
	.read    = knote_read,
	.write   = knote_write,
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
	dev->tail = dev->data;

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

	kn_dev->tail_rest = 0;
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
