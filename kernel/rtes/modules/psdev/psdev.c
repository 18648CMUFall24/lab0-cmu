/**
 * ./rc.local
 * References: https://lwn.net/Kernel/LDD3/ Chapters 3 and 6
 */
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/spinlock.h>

// Global variables
int psdev_major;
int psdev_minor = 0;
// Initialize spinlock
DEFINE_SPINLOCK(psdev_u_lock);
int psdev_u_count;
int psdev_u_owner;
struct file_operations psdev_fops; // function defined at end

/**
 * @brief Device registration that represents the device
 */
struct psdev_dev
{
    struct psdev_qset *data; /* Pointer to first quantum set (aka memory) */
    int quantum;             /* the current quantum size. each memory area is a quantum and the array (or its length) a quantum set. */
    int qset;                /* the current array size */
    unsigned long size;      /* amount of data stored here */
    unsigned int access_key; /* used by psdevuid and psdevpriv */
    struct semaphore sem;    /* mutual exclusion semaphore */
    struct cdev cdev;        /* Char device structure */
};

/**
 * @brief Dynamically allocate a major number
 */
int alloc_major_version(void)
{
    dev_t dev;
    unsigned int firstminor = 0; // requested first minor number to use; it is usually 0
    unsigned int count = 4;      // total number of contiguous device numbers you are requesting

    // dynamically-allocate device numbers
    int result = alloc_chrdev_region(&dev, firstminor, count, "psdev");
    // MAJOR extracts the major from a device number.
    psdev_major = MAJOR(dev);
    if (result < 0)
    {
        printk(KERN_WARNING "psdev: can't get major %d\n", psdev_major);
        return result;
    }
    return 0; // success
}

/**
 * @brief Register the device with the kernel
 */
static void psdev_setup_cdev(struct psdev_dev *dev, int index)
{
    // MKDEV: takes major and minor numbers and turns it into a dev_t,
    int err, devno = MKDEV(psdev_major, psdev_minor + index);
    // embed the cdev structure within a device-specific structure
    cdev_init(&dev->cdev, &psdev_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &psdev_fops;
    // Add the device to the kernel
    err = cdev_add(&dev->cdev, devno, 1);

    /* Fail gracefully if need be */
    if (err)
        printk(KERN_NOTICE "Error %d adding psdev%d", err, index);
}

/**
 * @brief Initialize the device on open
 */
int psdev_open(struct inode *inode, struct file *filp)
{
    // Use spinlock to only allow one user to open the device per file
    spin_lock(&psdev_u_lock);
    if (psdev_u_count &&
        (psdev_u_owner != current->uid) &&  /* allow user */
        (psdev_u_owner != current->euid) && /* allow whoever did su */
        !capable(CAP_DAC_OVERRIDE))
    { /* still allow root */
        spin_unlock(&psdev_u_lock);
        return -EBUSY; /* -EPERM would confuse the user */
    }
    if (psdev_u_count == 0)
        psdev_u_owner = current->uid; /* grab it */
    psdev_u_count++;
    spin_unlock(&psdev_u_lock);

    struct psdev_dev *dev; /* device information */
    // Container_of returns a pointer to the structure that contains the cdev structure
    dev = container_of(inode->i_cdev, struct psdev_dev, cdev);
    filp->private_data = dev; /* for other methods */

    /* now trim to 0 the length of the device if open was write-only */
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
    {
        printk(KERN_INFO "INSIDE OPEN");
    }
    return 0; /* success */
}

/**
 * @brief Release the device
 * Only one release call for each open
 * Not every close system call causes the release method to be invoked
 */
int psdev_release(struct inode *inode, struct file *filp)
{
    spin_lock(&psdev_u_lock);
    psdev_u_count--; /* nothing else */
    spin_unlock(&psdev_u_lock);
    return 0;
}

// int psdev_trim(struct psdev_dev *dev)
// {
//     struct psdev_qset *next, *dptr;
//     int qset = dev->qset;
//     int i; /* "dev" is not-null */
//     for (dptr = dev->data; dptr; dptr = next)
//     { /* all the list items */
//         if (dptr->data)
//         {
//             for (i = 0; i < qset; i++)
//                 kfree(dptr->data[i]);
//             kfree(dptr->data);
//             dptr->data = NULL;
//         }
//         next = dptr->next;
//         kfree(dptr);
//     }
//     dev->size = 0;
//     dev->quantum = psdev_quantum;
//     dev->qset = psdev_qset;
//     dev->data = NULL;
//     return 0;
// }

// ssize_t psdev_read(struct file *filp, char __user *buf, size_t count,
//                    loff_t *f_pos)
// {
//     struct psdev_dev *dev = filp->private_data;
//     struct psdev_qset *dptr; /* the first listitem */
//     int quantum = dev->quantum, qset = dev->qset;
//     int itemsize = quantum * qset; /* how many bytes in the listitem */
//     int item, s_pos, q_pos, rest;
//     ssize_t retval = 0;
//     if (down_interruptible(&dev->sem))
//         return -ERESTARTSYS;
//     if (*f_pos >= dev->size)
//         goto out;
//     if (*f_pos + count > dev->size)
//         count = dev->size - *f_pos;
//     /* find listitem, qset index, and offset in the quantum */
//     item = (long)*f_pos / itemsize;
//     rest = (long)*f_pos % itemsize;
//     s_pos = rest / quantum;
//     q_pos = rest % quantum;
//     /* follow the list up to the right position (defined elsewhere) */
//     dptr = psdev_follow(dev, item);
//     if (dptr == NULL || !dptr->data || !dptr->data[s_pos])
//         goto out; /* don't fill holes */
//     /* read only up to the end of this quantum */
//     if (count > quantum - q_pos)
//         count = quantum - q_pos;
//     if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count))
//     {
//         retval = -EFAULT;
//         goto out;
//     }
//     *f_pos += count;
//     retval = count;
// out:
//     up(&dev->sem);
//     return retval;
// }

// ssize_t psdev_write(struct file *filp, const char __user *buf, size_t count,
//                     loff_t *f_pos) struct psdev_dev *dev = filp -> private_data;
// {
//     struct psdev_qset *dptr;
//     int quantum = dev->quantum, qset = dev->qset;
//     int itemsize = quantum * qset;
//     int item, s_pos, q_pos, rest;
//     ssize_t retval = -ENOMEM; /* value used in "goto out" statements */
//     if (down_interruptible(&dev->sem))
//         return -ERESTARTSYS;
//     /* find listitem, qset index and offset in the quantum */
//     item = (long)*f_pos / itemsize;
//     rest = (long)*f_pos % itemsize;
//     s_pos = rest / quantum;
//     q_pos = rest % quantum;
//     /* follow the list up to the right position */
//     dptr = psdev_follow(dev, item);
//     if (dptr == NULL)
//         goto out;
//     if (!dptr->data)
//     {
//         dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
//         if (!dptr->data)
//             goto out;
//         memset(dptr->data, 0, qset * sizeof(char *));
//     }
//     if (!dptr->data[s_pos])
//     {
//         dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
//         if (!dptr->data[s_pos])
//             goto out;
//     }
//     /* write only up to the end of this quantum */
//     if (count > quantum - q_pos)
//         count = quantum - q_pos;
//     if (copy_from_user(dptr->data[s_pos] + q_pos, buf, count))
//     {
//         retval = -EFAULT;
//         goto out;
//     }
//     *f_pos += count;
//     retval = count;
//     /* update the size */
//     if (dev->size < *f_pos)
//         dev->size = *f_pos;
// out:
//     up(&dev->sem);
//     return retval;
// }

/**
 * @brief File Operations
 */
struct file_operations psdev_fops = {
    .owner = THIS_MODULE,
    // .llseek = psdev_llseek,
    // .read = psdev_read,
    // .write = psdev_write,
    // .ioctl = psdev_ioctl,
    .open = psdev_open,
    .release = psdev_release};

int main(void)
{
    struct psdev_dev dev;
    alloc_major_version();
    psdev_setup_cdev(&dev, 0);
    return 0;
}
