/**
 * 4.4  Character Device Driver (15 points)
 * SOURCE-CODE-LOCATION: kernel/rtes/modules/psdev/psdev.c
 * Create a moudle with a device that when read will output a list of
 * real-time threads with their thread ID, process IDs, real-time priority,
 * and command name. Reading from your device with e.g. cat /dev/psdev should
 * print the list of real-time threads.
 * 
 *  tid  pid  prio  command
 *  102  102   90    adbd
 *  103  103   80    pigz
 *  104  103   80    pigz
 * 
 * The user should bbe able to create and use MULTIPLE, INDEPENDENT instances.
 * The number of file descriptors that can be associated with one devices instace 
 * should be limited to one. When the limit is exceeded, the "open" system call must
 * fail with EBUSY. Operations that are part of the file abstraction, but do not make
 * sense for the device, should fail with ENOTSUPP. As with any file, the user
 * should be able to read the contents in chunks over multiple read operations.
 * NOTE: The file abstraction allows the user to issue concurrent reads on the same
 * file descriptor.
 * 
 * Commands:
 *     a) Using make: `~/lab0-cmu/kernel/rtes/modules/psdev$ make`
 *     b) Using make M=: `~/lab0-cmu$ make M=kernel/rtes/modules/psdev`
 * 
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/types.h>  

MODULE_LICENSE("Dual BSD/GPL");

#define DEVICE_NAME "psdev"     
#define MAX_DEVICE_INSTANCES 5   /* Max number of minor device */
#define MAX_BUFFER_SIZE 1024     /* Max buffer size for device */

static int psdev_open(struct inode *inode, struct file *filp);
static int psdev_release(struct inode *inode, struct file *filp);
static ssize_t psdev_read(struct file *filp, char __user *buf, size_t count, loff_t *fops);

/* initialize file operations */
static const struct file_operations psdev_fops = {
    .owner = THIS_MODULE,
    .read = psdev_read,
    .open = psdev_open,
    .release = psdev_release,
};

/* device informatino register */
struct psdev_data {
    struct cdev cdev;       /* Character device structure*/
    struct mutex mutex;     /* Mutex for device */
    int is_open;            /* Device is open */ 
    char *data;             /* Data buffer to store ouptup */ 
    size_t data_size;       /* Size of output data */
};

// global storage for major number
static int psdev_major = 0;

// sysfs class for device
static struct class *psdev_class = NULL;

static struct psdev_data mypsdev_data[MAX_DEVICE_INSTANCES];

static int psdev_open(struct inode *inode, struct file *filp) 
{
    struct psdev_data *psdev;
    psdev = container_of(inode->i_cdev, struct psdev_data, cdev);

    if (psdev->is_open) {
        mutex_unlock(&psdev->mutex);
        return -EBUSY;                  /* access limit is exceeded */    
    }

    psdev->is_open = 1;
    filp->private_data = psdev;

    psdev->data = kmalloc(MAX_BUFFER_SIZE, GFP_KERNEL);
    if(!psdev->data) {
        return -ENOMEM;                 /* memory allocation fail */
    }

    
    // print rt thread info here
    //gather_rt_thread_info(psdev);

    return 0;
}

static int psdev_release(struct inode *inode, struct file *filp) 
{
    struct psdev_data *psdev = filp->private_data;

    kfree(psdev->data);
    psdev->is_open = 0;
    mutex_unlock(&psdev->mutex);

    return 0;
}

static ssize_t read(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    struct psdev_data *psdev = filp->private_data;
    ssize_t retval = 0;

    if (*f_pos >= psdev->data_size) {
        return 0;       // EOF
    }

    retval = min(count, psdev->data_size - *f_pos);
    if (copy_to_user(buf, psdev->data + *f_pos, retval)) {  /* print to user space */
        return -EFAULT;
    }

    *f_pos += retval;
    return retval;
}

static int __init psdev_init(void) {
    int err, i;
    dev_t psdev;

    err = alloc_chrdev_region(&psdev, 0, MAX_DEVICE_INSTANCES, "psdev")

    if (err < 0) {
        printk(KERN_WARNING "Failed to get major\n");
    }

    int psdev_major = MAJOR(psdev);
    for (i = 0; i < MAX_DEVICE_INSTANCES; i++) {
        dev_init(&psdev_data[i].cdev, &psdev_fops);
        psdev_data[i].cdev.owner = THIS_MODULE;
        psdev_data[i].is_open = 0;
        mutex_init(&psdev_data[i].lock);

        err = cdev_add(&psdev_data[i].cdev, MKDEV(psdev_major, i), 1);

        if (err) {
            printk(KERN_NOTICE "Error %d adding psdev%d", result, i);
            return err;
        }
    }
    printk(KERN_INFO "psdev: registered with major number %d\n", major);
    return 0;
    
}   
