#include <linux/init.h>
#include <linux/module.h>
/* MODULE_XXX              */
/* THIS_MODULE             */
#include <linux/kdev_t.h>
/* MKDEV                   */
#include <linux/fs.h>
/* register_chrdev_region  */
/* struct file_operations  */
#include <linux/cdev.h>
/* struct cdev             */
/* cdev_alloc              */
#include <linux/kernel.h>
/* container_of            */
#include <linux/slab.h>
/* kmalloc & kfree         */
#include <asm/uaccess.h>
/* copy_to_user            */
/* copy_from_user          */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Incarnation P. Lee <incarnation.p.lee@gmail.com>");

#define SCULL_MAJOR     301
#define SCULL_MINOR     1
#define SCULL_NAME      "scull"

/* INTERNAL SCULL STRUCTRUE */
struct scull_qset {
	void **data;
	struct scull_qset *next;
}; /* scull_dev memory layout
   scull_dev->data
              |-struct scull_qset No.1
               \/-data
		\-next => struct scull_qset No.2
                          \/-data
		           \-next => struct scull_qset No.3
                                     \/-data
		                      \-next */

struct scull_dev {
	struct scull_qset *data;  /* Pointer to first quantum set */
	int quantum;              /* The current quantum size */
        int qset;                 /* The current array size */
        unsigned long size;       /* Amount of data stored scullpriv */
        unsigned int access_key;  /* Used by sculluid and scullpriv */
        struct semaphore sem;     /* Mutual exclusion semaphore */
        struct cdev cdev;         /* Char device structure */
}; /* end of INTERNAL SCULL STRUCTRUE */


static struct scull_dev *sp_scull_dev;
static unsigned int count;
/* each quantum contains 16 bytes */
static int scull_quantum = 16;
/* each qset array contains 32 quantum */
static int scull_qset    = 12;


int scull_trim(struct scull_dev *dev)
{
	struct scull_qset *next, *dptr;
	int qset = dev->qset;
	int i;

	for (dptr = dev->data; dptr; dptr = next) {
		if (dptr->data) {
			for (i = 0; i < qset; i++) {
				if (dptr->data[i]) {
					kfree(dptr->data[i]);
					pr_err("Free memory of quantum %2d........ [OK]\n", i);
				}
			}
			kfree(dptr->data); /* free the array pointer here, qset */
			dptr->data = NULL;
		}
		next = dptr->next;
		kfree(dptr);
	}

	dev->size = 0;
	dev->quantum = scull_quantum;
	dev->qset = scull_qset;
	dev->data = NULL;

	return 0;
}

struct scull_qset * scull_follow(struct scull_dev *dev, int n)
{
	struct scull_qset *qset = dev->data;

	/* Allocate first scull_qset */
	if (!qset) {
		dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
		qset = dev->data;
		if (!qset)
			return NULL;
		memset(qset, 0, sizeof(struct scull_qset));
	}

	/* Then the following list */
	while (n--) {
		if (!qset->next) {
			qset->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
			if (!qset->next)
				return NULL;
			memset(qset->next, 0, sizeof(struct scull_qset));
		}
		qset = qset->next;
	}
	return qset;
}


/* start of SCULL OPERATION FUNCTION */
int scull_open(struct inode *inode, struct file *filp)
{
	struct scull_dev *dev;

	dev = container_of(inode->i_cdev, struct scull_dev, cdev);
	filp->private_data = dev;

	if (O_WRONLY == (filp->f_flags & O_ACCMODE)) {
		scull_trim(dev);
	}

	return 0;
}

int scull_release(struct inode *inode, struct file *filp)
{
	return 0;
}

ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum = dev->quantum;
        int qset = dev->qset;
	int itemsize = quantum * qset;
	int item, s_pos, q_pos, rest;
	ssize_t retval = 0;

	if (*f_pos >= dev->size)
		goto OUT;
	if (*f_pos + count > dev->size)
		count = dev->size - *f_pos;

	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	dptr = scull_follow(dev, item);
	if (NULL == dptr || !dptr->data || !dptr->data[s_pos])
		goto OUT;

	if (count > quantum - q_pos)
		count = quantum - q_pos;

	if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
		retval = -EFAULT;
		goto OUT;
	}

	*f_pos += count;
	retval = count;

OUT:
	return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum = dev->quantum;
	int qset = dev->qset;
	int itemsize = quantum * qset;
	int item, s_pos, q_pos, rest;
	ssize_t retval = -ENOMEM;

	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	dptr = scull_follow(dev, item);
	if (NULL == dptr)
		goto OUT;

	if (!dptr->data) {
		dptr->data = kmalloc(qset * sizeof(char*), GFP_KERNEL);
	        pr_err("Alloc memory for scull_qset........ [OK]\n");
		if (!dptr->data)
			goto OUT;
		memset(dptr->data, 0, qset * sizeof(char*));
	}

	if (!dptr->data[s_pos]) {
		dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
	        pr_err("Alloc memory for quantum %2d........ [OK]\n", s_pos);
		if (!dptr->data[s_pos])
			goto OUT;
	}

	if (count > quantum - q_pos)
		count = quantum - q_pos;

	if (copy_from_user(dptr->data[s_pos] + q_pos, buf, count)) {
		retval = -EFAULT;
		goto OUT;
	}

	*f_pos += count;
	retval = count;

	if (dev->size < *f_pos)
		dev->size = *f_pos;

OUT:
	return retval;
}
/* end of SCULL OPERATION FUNCTION */


static struct file_operations scull_fops = {
	.owner =   THIS_MODULE,
	.open =    scull_open,
	.release = scull_release,
	.read =    scull_read,
	.write =   scull_write,
/*
	.llseek =  scull_llseek,
	.ioctl =   scull_ioctl,
*/
};


/*
  Initialize and add cdev to the system
*/
static void scull_setup_cdev(struct scull_dev *dev, int index)
{
	int err, devno;

        devno = MKDEV(SCULL_MAJOR, SCULL_MINOR + index);
	cdev_init(&dev->cdev, &scull_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &scull_fops;
	err = cdev_add(&dev->cdev, devno, 1);
	if (err)
		pr_err("Error %d adding scull%d\n", err, index);
}

static void scull_setup_devno(void)
{
	int err;
	dev_t devno;

	count = 1;
	devno = MKDEV(SCULL_MAJOR, SCULL_MINOR);
	err = register_chrdev_region(devno, count, SCULL_NAME);
	if (err)
		pr_err("Error %d adding %s\n", err, SCULL_NAME);
}


static int scull_setup_quantum_alloc(struct scull_dev *dev)
{
	int result;
	void **tmp;
	struct scull_qset *qset;

	qset = kmalloc(sizeof(scull_qset), GFP_KERNEL);
	if (!qset) {
		result = -ENOMEM;
		goto FAIL;
	}

	dev->data = qset;
	qset->next = NULL;
	qset->data = (void**)kmalloc(sizeof(void*) * dev->qset, GFP_KERNEL);
	pr_err("Alloc memory for scull_qset........ [OK]\n");

	if (!qset->data) {
		result = -ENOMEM;
		kfree(qset);
		goto FAIL;
	}
	memset(qset->data, 0, sizeof(void*) * dev->qset);
	pr_err("Alloc memory for scull_qset->data array........ [OK]\n");

	tmp = qset->data;
	while(tmp < qset->data + dev->qset) {
		*tmp = kmalloc(dev->quantum, GFP_KERNEL);
	        memset(*tmp, 0, dev->quantum);
		if (!tmp) {
			kfree(qset->data);
			kfree(qset);
			result = -ENOMEM;
		}
		pr_err("Alloc memory for quantum %2d........ [OK]\n", (int)(tmp - qset->data));
		tmp++;
	}
FAIL:
	return result;
}

static int setup_scull(void)
{
	int result;
	scull_setup_devno();

	result = 0;
	sp_scull_dev = kmalloc(sizeof(struct scull_dev), GFP_KERNEL);
	if (!sp_scull_dev) {
		result = -ENOMEM;
		goto FAIL;
	}
	pr_err("Alloc memory for scull_dev........ [OK]\n");

	sp_scull_dev->quantum = scull_quantum;
	sp_scull_dev->qset = scull_qset;
	scull_setup_quantum_alloc(sp_scull_dev);
	scull_setup_cdev(sp_scull_dev, 0);

FAIL:
	return result;
}


static void cleanup_scull_cdev(void)
{
	dev_t devno;

	devno = MKDEV(SCULL_MAJOR, SCULL_MINOR);
	cdev_del(&sp_scull_dev->cdev);
	unregister_chrdev_region(devno, count);
}

static int __init scull_init(void)
{
	int result;

	pr_err(">>>>> LOADING MODULE scull.ko\n");
	result = setup_scull();

	if (-ENOMEM == result) {
		cleanup_scull_cdev();
		scull_trim(sp_scull_dev);
		kfree(sp_scull_dev);
	}

	return result;
}

static void __exit scull_exit(void)
{
	cleanup_scull_cdev();
	scull_trim(sp_scull_dev);
	kfree(sp_scull_dev);
	pr_err(">>>>> REMOVING MODULE scull.ko\n");
}

module_init(scull_init);
module_exit(scull_exit);
