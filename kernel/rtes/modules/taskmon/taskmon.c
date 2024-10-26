/**
 * Kernel Instrumentation for Task Utilization Measurement (25 points)
 *  Add instrumentation functionality to the kernel for estimating computation demands of threads. The kernel
 *  should support collecting utilization values for threads with active reservations during a user-initiated mon-
 *  itoring session. A utilization value for a given period is the computation time used by the thread during
 *  that period.
 *
 * Resources:
 *   - https://www.kernel.org/doc/Documentation/kobject.txt
 *   - https://pradheepshrinivasan.github.io/2015/07/02/Creating-an-simple-sysfs/
 *
 * Run (on target device)
 *      adb push taskmon.ko /data
 *      adb shell
 *
 *      root# cd data/
 *      root# insmod ./taskmon.ko
 *      root# dmesg | tail
 *      root# rmmod taskmon
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

MODULE_LICENSE("Dual BSD/GPL");

static bool taskmon_enabled = false;
static struct kobject *rtes_kobj; // kobject for /rtes

// When the user reads the sysfs file
static ssize_t taskmon_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    // Return the current value of taskmon_enabled as 0 or 1
    return sprintf(buf, "%d\n", (int)taskmon_enabled);
}

// When the user writes to the sysfs file
static ssize_t taskmon_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    // Set taskmon_enabled to true or false and start or stop monitoring accordingly
    if (buf[0] == '1')
    {
        taskmon_enabled = true;
        // Start monitoring
        printk(KERN_INFO "Taskmon enabled\n");
    }
    else if (buf[0] == '0')
    {
        taskmon_enabled = false;
        // Stop monitoring
        printk(KERN_INFO "Taskmon disabled\n");
    }
    return count;
}

/** Initializes the kobj_attribute struct with the taskmon_show and taskmon_store functions
 *   - enabled is the name of the sysfs file
 *   - 0644 is the permissions of the sysfs file. Read/write for root, read-only for others
 *   - taskmon_show is the function to call when the user reads the sysfs file
 *   - taskmon_store is the function to call when the user writes to the sysfs file
 */
static struct kobj_attribute taskmon_attr = __ATTR(enabled, 0644, taskmon_show, taskmon_store);

// Init kernel module
static int taskmon_init(void)
{
    // Create a kobject named "rtes" under the kernel kobject
    rtes_kobj = kobject_create_and_add("rtes", kernel_kobj);
    if (!rtes_kobj)
        return -ENOMEM; // Error NO MEMory

    // Create a sysfs file named "taskmon" under the "rtes" kobject
    int error = sysfs_create_group(rtes_kobj, &taskmon_attr);
    if (error)
        kobject_put(rtes_kobj); // Release the kobject if there was an error

    return error;
}

// Exit kernel module
static void taskmon_exit(void)
{
    // Remove the sysfs file and release the kobject
    kobject_put(rtes_kobj);
}

module_init(taskmon_init);
module_exit(taskmon_exit);