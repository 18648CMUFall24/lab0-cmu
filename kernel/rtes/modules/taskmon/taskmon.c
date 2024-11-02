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
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL"); // Required to use some functions

static bool taskmon_enabled = false;
static struct kobject *rtes_kobj;    // kobject for /rtes
static struct kobject *taskmon_kobj; // kobject for /rtes/taskmon
static struct kobject *util_kobj;    // kobject for /rtes/taskmon/util

struct tid_attr_node
{
    struct kobj_attribute *attr;
    struct tid_attr_node *next;
};
// Store the list of TID attributes
struct tid_attr_node *tid_attr_list = NULL;

// When the user reads the sysfs file
static ssize_t enabled_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    // Return the current value of taskmon_enabled as 0 or 1
    return sprintf(buf, "%d\n", (int)taskmon_enabled);
}

// When the user writes to the sysfs file
static ssize_t enabled_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
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

// When the user reads the sysfs file
static ssize_t tid_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    // Return the current value of taskmon_enabled as 0 or 1
    return sprintf(buf, "%s\n%s\n%s\n", "10 0.5", "14 0.25", "18 0.25");
}

/** Initializes the kobj_attribute struct with the enabled_show and enabled_store functions
 *   - enabled is the name of the sysfs file
 *   - 0660 is the permissions of the sysfs file, owner can read/write, group can read/write, others can't access
 *   - enabled_show is the function to call when the user reads the sysfs file
 *   - enabled_store is the function to call when the user writes to the sysfs file
 */
static struct kobj_attribute enabled_attr = __ATTR(enabled, 0660, enabled_show, enabled_store);

int init_kobjects(void)
{
    // Create a kobject named "rtes" under the kernel kobject
    rtes_kobj = kobject_create_and_add("rtes", NULL); // use NULL for /sys/ instead of kernel_kobj); which is /sys/kernel
    if (!rtes_kobj)
    {
        printk(KERN_ERR "Failed to create kobject: rtes\n");
        return -ENOMEM; // Error NO MEMory
    }
    // Create a kobject named "taskmon" under the "rtes" kobject
    taskmon_kobj = kobject_create_and_add("taskmon", rtes_kobj);
    if (!taskmon_kobj)
    {
        printk(KERN_ERR "Failed to create kobject: taskmon\n");
        kobject_put(rtes_kobj); // Release the kobject
        return -ENOMEM;
    }
    // Create a kobject named "util" under the "taskmon" kobject
    util_kobj = kobject_create_and_add("util", taskmon_kobj);
    if (!util_kobj)
    {
        printk(KERN_ERR "Failed to create kobject: util\n");
        kobject_put(rtes_kobj);
        kobject_put(taskmon_kobj);
        return -ENOMEM;
    }
    return 0;
}

void release_kobjects(void)
{
    // Release the kobjects
    kobject_put(rtes_kobj);
    kobject_put(taskmon_kobj);
    kobject_put(util_kobj);
}

void free_tid_attr_list(void)
{
    struct tid_attr_node *node = tid_attr_list;
    struct tid_attr_node *next;
    while (node)
    {
        next = node->next;
        kfree(node->attr->attr.name);
        kfree(node->attr);
        kfree(node);
        node = next;
    }
}

// Create the sysfs file /sys/rtes/taskmon/enabled
int create_enabled_file(void)
{
    int ret;
    // Create a sysfs file named "enabled" under the "taskmon" kobject
    ret = sysfs_create_file(taskmon_kobj, &enabled_attr.attr);
    if (ret)
    {
        printk(KERN_ERR "Failed to create file: /sys/rtes/taskmon/enabled\n");
        kobject_put(rtes_kobj);
        kobject_put(taskmon_kobj);
        return -1;
    }
    printk(KERN_INFO "Created file: /sys/rtes/taskmon/enabled\n");
    return 0; // Success
}

// Create /sys/rtes/taskmon/util/<tid>
int create_tid_file(int tid)
{
    int ret;
    struct kobj_attribute *tid_attr;
    struct tid_attr_node *new_node;

    // Allocate memory for the attribute
    tid_attr = kzalloc(sizeof(*tid_attr), GFP_KERNEL); // alloc and init to zero
    if (!tid_attr)
        return -ENOMEM;

    // Initialize the attribute
    tid_attr->attr.name = kasprintf(GFP_KERNEL, "%d", tid);
    tid_attr->attr.mode = 0444; // Read-only
    tid_attr->show = tid_show;

    // Create the sysfs file
    ret = sysfs_create_file(util_kobj, &tid_attr->attr);
    if (ret)
    {
        printk(KERN_ERR "Failed to create file: /sys/rtes/taskmon/util/%d\n", tid);
        kfree(tid_attr->attr.name);
        kfree(tid_attr);
        return -1;
    }
    // Add attribute to the list
    new_node = kzalloc(sizeof(*new_node), GFP_KERNEL);
    new_node->attr = tid_attr;
    new_node->next = tid_attr_list;
    tid_attr_list = new_node;

    printk(KERN_INFO "Created file: /sys/rtes/taskmon/util/%d\n", tid);
    return 0; // Success
}

// Init kernel module
static int __init taskmon_init(void)
{
    int i;

    if (init_kobjects() != 0)
        return -1;

    if (create_enabled_file() != 0)
        return -1;

    // Mock create tid files from 0 to 9
    for (i = 0; i < 10; i++)
    {
        if (create_tid_file(i) != 0)
            return -1;
    }
    return 0;
}

// Exit kernel module
static void __exit taskmon_exit(void)
{
    release_kobjects();
    free_tid_attr_list();
}

module_init(taskmon_init);
module_exit(taskmon_exit);