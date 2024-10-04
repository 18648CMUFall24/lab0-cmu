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
#include <linux/rwlock.h>
#include <asm/errno.h>
#include <linux/rcupdate.h>

MODULE_LICENSE("Dual BSD/GPL");

#define DEVICE_NAME "psdev"     
#define MAX_DEVICE_INSTANCES 5   /* Max number of minor device */
#define MAX_BUFFER_SIZE 1024     /* Max buffer size for device */

/* device informatino register */
struct psdev_data {
    struct cdev cdev;       /* Character device structure*/
    struct mutex mutex;     /* Mutex for device */
    int is_open;            /* Device is open */ 
    char *data;             /* Data buffer to store ouptup */ 
    size_t data_size;       /* Size of output data */
};

static int psdev_open(struct inode *inode, struct file *filp);
static int psdev_release(struct inode *inode, struct file *filp);
static ssize_t psdev_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
static long psdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static void gather_rt_thread_info(struct psdev_data *dev);

/* initialize file operations */
static const struct file_operations psdev_fops = {
    .owner = THIS_MODULE,
    .read = psdev_read,
    .open = psdev_open,
    .release = psdev_release,
    .unlocked_ioctl = psdev_ioctl,
};

static int psdev_major;     // stores major number

static struct psdev_data mypsdev_data[MAX_DEVICE_INSTANCES];    // for each device instance


// open device: lock, check if device is open, allocate memory, gather rt thread info, unlock
static int psdev_open(struct inode *inode, struct file *filp) 
{
    struct psdev_data *psdev;
    psdev = container_of(inode->i_cdev, struct psdev_data, cdev);
    mutex_lock(&psdev->mutex);

    if (psdev->is_open) {
        mutex_unlock(&psdev->mutex);
        return -EBUSY;                  /* access limit is exceeded */    
    }

    psdev->is_open = 1;
    filp->private_data = psdev;

    psdev->data = kmalloc(MAX_BUFFER_SIZE, GFP_KERNEL);
    if(!psdev->data) {
        psdev->is_open = 0;
        mutex_unlock(&psdev->mutex);
        return -ENOMEM;                 /* memory allocation fail */
    }
    
    memset(psdev->data, 0, MAX_BUFFER_SIZE);
    // print rt thread info here
    gather_rt_thread_info(psdev);
    mutex_unlock(&psdev->mutex);
    return 0;
}

// close device: lock, free memory, unset flag, unlock
static int psdev_release(struct inode *inode, struct file *filp) 
{
    struct psdev_data *psdev = filp->private_data;
    mutex_lock(&psdev->mutex);
    kfree(psdev->data);
    psdev->is_open = 0;
    mutex_unlock(&psdev->mutex);

    return 0;
}

// read device: lock, copy data to user space, updata file position, unlock
static ssize_t psdev_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    struct psdev_data *psdev = filp->private_data;
    ssize_t retval = 0; // how much data to read

    if (*f_pos >= psdev->data_size) {
        return 0;       // EOF
    }

    retval = min(count, (size_t)(psdev->data_size - *f_pos));
    if (copy_to_user(buf, psdev->data + *f_pos, retval)) {  /* print to user space */
        return -EFAULT;
    }

    *f_pos += retval;
    return retval;
}

static long psdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    return -ENOTSUPP; // Operation not supported
}

// alloc major and minor number, iinitilize devices, and register device to kernel
static int __init psdev_init(void) {
    int err, i;
    dev_t psdev;

    err = alloc_chrdev_region(&psdev, 0, MAX_DEVICE_INSTANCES, "psdev");

    if (err < 0) {
        printk(KERN_WARNING "Failed to get major\n");
        return err;
    }

    psdev_major = MAJOR(psdev);

    for (i = 0; i < MAX_DEVICE_INSTANCES; i++) {
        cdev_init(&mypsdev_data[i].cdev, &psdev_fops);
        mypsdev_data[i].cdev.owner = THIS_MODULE;
        mypsdev_data[i].is_open = 0;
        mutex_init(&mypsdev_data[i].mutex);

        err = cdev_add(&mypsdev_data[i].cdev, MKDEV(psdev_major, i), 1);

        if (err) {
            printk(KERN_NOTICE "Error %d adding psdev%d", err, i);
            return err;
        }
    }
    printk(KERN_INFO "psdev: registered with major number %d\n", psdev_major);
    return 0;
}   

static void __exit psdev_exit(void) {
    int i = 0;

    for (i = 0; i < MAX_DEVICE_INSTANCES; i++) {
        cdev_del(&mypsdev_data[i].cdev);
    }

    unregister_chrdev_region(MKDEV(psdev_major, 0), MAX_DEVICE_INSTANCES);
    printk(KERN_INFO "psdev: unregistered devices\n");
}

static void gather_rt_thread_info(struct psdev_data *dev) {
    struct task_struct *task, *t;       // task handle process,  t handle thread
    size_t offset = 0;
    int buffer_flag = 0;

    rcu_read_lock(); // Start the read-side critical section
    offset += snprintf(dev->data + offset, MAX_BUFFER_SIZE - offset, 
                       "%6s %6s %6s %8s\n", "tid", "pid", "pr", "name");

    for_each_process(task) {
        t = task;

        while(1) {
            if (t->rt_priority > 0) {       // only print real-time threads
                offset += snprintf(dev->data + offset, MAX_BUFFER_SIZE - offset,
                                   "%6d  %6d   %4d   %s\n",
                                   t->pid, task->tgid, t->rt_priority, t->comm);
                
                if (offset >= MAX_BUFFER_SIZE) {
                    printk(KERN_WARNING "psdev: Buffer size exceeded\n");
                    buffer_flag = 1;
                    break;
                }
            }

            t = next_thread(t);
            if (t == task) {
                break;
            }
        }

        if (buffer_flag) {
            break;
        }
    }

    rcu_read_unlock(); // End the read-side critical section 
    dev->data_size = offset;       
}
    
module_init(psdev_init);
module_exit(psdev_exit);
