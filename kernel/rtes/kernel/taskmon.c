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
 * Requirements:
 *  1. Create a sysfs file /sys/rtes/taskmon/enabled that can be used to enable or disable the task monitoring
 *     functionality. The file should be writable and readable. When enabled, the kernel should start collecting
 *     utilization values for threads with active reservations. When disabled, the kernel should stop collecting
 *     utilization values.
 *  2. Create a sysfs file /sys/rtes/taskmon/util/<TID> that can be used to read the utilization values for a
 *     thread with the given TID. The file should be readable and should return the utilization values for the
 *     thread in the following format: (ms, util)
 */

#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/math64.h>
#include <linux/reservation.h> // For struct data_point
#include "taskmon.h"


bool taskmon_enabled = false;
struct kobject *rtes_kobj;    // kobject for /rtes
struct kobject *taskmon_kobj; // kobject for /rtes/taskmon
struct kobject *util_kobj;    // kobject for /rtes/taskmon/util

struct tid_attr_node
{
    struct kobj_attribute *attr;
    struct tid_attr_node *next;
};

// Store the list of TID attributes
static struct tid_attr_node *tid_attr_list = NULL;

// Mutex to protect the tid_attr_list
static DEFINE_MUTEX(tid_attr_list_mutex);

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
        enable_monitoring_for_all_tasks();
        // Start monitoring
        printk(KERN_INFO "Taskmon enabled\n");
    }
    else if (buf[0] == '0')
    {
        taskmon_enabled = false;
        disable_monitoring_for_all_tasks();
        // Stop monitoring
        printk(KERN_INFO "Taskmon disabled\n");
    }
    return count;
}

// When the user reads the sysfs file
static ssize_t tid_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    // // Return the current value of taskmon_enabled as 0 or 1
    // return sprintf(buf, "%s\n%s\n%s\n", "10 0.5", "14 0.25", "18 0.25");
    struct task_struct *task;
    struct reservation_data *res_data;
    const char *file_name;
    struct data_point *point;
    ssize_t len = 0;
    ssize_t temp_len;
    pid_t pid = -1;
    unsigned long flags;
    int ret;

    file_name = attr->attr.name; // file name
    printk(KERN_INFO "tid_show: file_name: %s\n", file_name);
    if ((ret = kstrtoint(file_name, 10, &pid)) != 0) {
        printk(KERN_ERR "tid_show: Failed to read pid (%d) from kobject name\n", pid);
        return ret;
    }

    // Find the task by PID
    rcu_read_lock();
    task = find_task_by_vpid(pid);
    if (!task) {
        printk(KERN_ERR "tid_show: find_task_by_vpid failed\n");
        rcu_read_unlock();
        return -ESRCH;
    }
    get_task_struct(task);
    rcu_read_unlock();

    printk(KERN_INFO "tid_show: PID = %d\n", pid);
    
    res_data = task->reservation_data;      // Get the reservation data
    if (!res_data) {
        put_task_struct(task);
        return snprintf(buf, PAGE_SIZE, "No reservation data available\n");
    }

    spin_lock_irqsave(&res_data->data_lock, flags);
    if (list_empty(&res_data->data_points)) {
        spin_unlock_irqrestore(&res_data->data_lock, flags);
        put_task_struct(task);

        return snprintf(buf, PAGE_SIZE, "No utilization data available yet\n");
    }

    // Iterate through the utilization data points and format them correctly
    list_for_each_entry(point, &res_data->data_points, list)
    {
        temp_len = snprintf(buf + len, PAGE_SIZE - len, "%llu %s\n",
                            point->timestamp,
                            point->utilization);
        if (temp_len < 0) {
            spin_unlock_irqrestore(&res_data->data_lock, flags);
            put_task_struct(task);
            return temp_len; // Return the error code if snprintf fails
        }

        len += temp_len;

        if (len >= PAGE_SIZE) {
            len = PAGE_SIZE; // Truncate output if we exceed the buffer size
            break;
        }
    }

    spin_unlock_irqrestore(&res_data->data_lock, flags);
    put_task_struct(task);

    printk(KERN_INFO "DEBUG: tid_show end, length: %zd\n", len);
    return len;
}

/** Initializes the kobj_attribute struct with the enabled_show and enabled_store functions
 *   - enabled is the name of the sysfs file
 *   - 0660 is the permissions of the sysfs file, owner can read/write, group can read/write, others can't access
 *   - enabled_show is the function to call when the user reads the sysfs file
 *   - enabled_store is the function to call when the user writes to the sysfs file
 */
static struct kobj_attribute enabled_attr = __ATTR(enabled, 0660, enabled_show, enabled_store);

int __init init_kobjects(void)
{
    // Create a kobject named "rtes" under the kernel kobject /sys
    rtes_kobj = kobject_create_and_add("rtes", NULL); // use NULL for /sys/ instead of kernel_kobj); which is /sys/kernel
    if (!rtes_kobj)
    {
        printk(KERN_ERR "Failed to create kobject: rtes\n");
        return -ENOMEM; // Error NO MEMory
    }
    // Create a kobject named "taskmon" under the "rtes" kobject /sys/rtes
    taskmon_kobj = kobject_create_and_add("taskmon", rtes_kobj);    
    if (!taskmon_kobj)
    {
        printk(KERN_ERR "Failed to create kobject: taskmon\n");
        kobject_put(rtes_kobj); // Release the kobject
        return -ENOMEM;
    }
    // Create a kobject named "util" under the "taskmon" kobject /sys/rtes/taskmon
    util_kobj = kobject_create_and_add("util", taskmon_kobj);
    if (!util_kobj)
    {
        printk(KERN_ERR "Failed to create kobject: util\n");
        kobject_put(rtes_kobj);
        kobject_put(taskmon_kobj);
        return -ENOMEM;
    }
    printk(KERN_INFO "Created kobject: /sys/rtes/taskmon/util\n");
    return 0;
}

// Release the kobjects during cleanup
void release_kobjects(void)
{
    if (taskmon_kobj) {
        sysfs_remove_file(taskmon_kobj, &enabled_attr.attr);
        // Release the kobjects
        kobject_put(util_kobj);
        kobject_put(taskmon_kobj);
        kobject_put(rtes_kobj);
        taskmon_kobj = NULL;
        util_kobj = NULL;
        rtes_kobj = NULL;
    }
}
    
// free the tid_attr_list
void free_tid_attr_list(void)
{
    struct tid_attr_node *node, *next;
    mutex_lock(&tid_attr_list_mutex);
    node = tid_attr_list;

    while (node)
    {
        next = node->next;
        // sysfs_remove_file(util_kobj, &node->attr->attr);
        kfree(node->attr->attr.name);
        kfree(node->attr);
        kfree(node);
        node = next;
    }
    tid_attr_list = NULL;
    mutex_unlock(&tid_attr_list_mutex);
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


// Create the sysfs file /sys/rtes/taskmon/util/<tid>
// create the kobj_attribute, create the sysfs file, add the attribute to the list, and link the attribute to the reservation data
int create_tid_file(struct task_struct *task)
{
    int ret;
    struct kobj_attribute *tid_attr;
    struct tid_attr_node *new_node;
    struct reservation_data *res_data = task->reservation_data;
    char tid_str[10];

    // Allocate memory for the attribute
    tid_attr = kzalloc(sizeof(*tid_attr), GFP_KERNEL); // alloc and init to zero
    if (!tid_attr)
        return -ENOMEM;
    // Initialize the attribute
    snprintf(tid_str, sizeof(tid_str), "%d", task->pid);
    // Initialize the attribute
    tid_attr->attr.name = kstrdup(tid_str, GFP_KERNEL);
    if (!tid_attr->attr.name) {
        printk(KERN_ERR "create_tid_file: Failed to allocate memory for attr.name\n");
        kfree(tid_attr);
        return -ENOMEM;
    }
    tid_attr->attr.mode = 0444; // Read-only
    tid_attr->show = tid_show;
    tid_attr->store = NULL;
    res_data->taskmon_tid_attr = tid_attr;

    // Create the sysfs file
    // ret = sysfs_create_file(res_data->taskmon_kobj, &tid_attr->attr);
    ret = sysfs_create_file(util_kobj, &tid_attr->attr);
    if (ret)
    {
        printk(KERN_ERR "create_tid_file: Failed to create file: /sys/rtes/taskmon/util/%d\n", task->pid);
        kfree(tid_attr->attr.name);
        kfree(tid_attr);
        return ret;
    }

    printk(KERN_INFO "create_tid_file: Created file: /sys/rtes/taskmon/util/%d\n", task->pid);

    // Add attribute to the list
    new_node = kzalloc(sizeof(*new_node), GFP_KERNEL);
    if (!new_node)
    {
        printk(KERN_ERR "create_tid_file: Failed to allocate memory for new_node\n");
        sysfs_remove_file(util_kobj, &tid_attr->attr);
        kfree(tid_attr->attr.name);
        kfree(tid_attr);
        return -ENOMEM;
    }
    new_node->attr = tid_attr;
    mutex_lock(&tid_attr_list_mutex);
    new_node->next = tid_attr_list;
    tid_attr_list = new_node;
    mutex_unlock(&tid_attr_list_mutex);
    printk(KERN_INFO "create_tid_file: Successfully created sysfs file /sys/rtes/taskmon/util/%d\n", task->pid);
    return 0; // Success
}

// remove /sys/rtes/taskmon/util/<tid>
// list tracersal to find the matching node by tid, remove the sysfs file, free the memory, and clear the attribute pointer in reservation data
int remove_tid_file(struct task_struct *task) {
    struct reservation_data *res_data = task->reservation_data;
    struct tid_attr_node *node, *prev = NULL;
    int ret = -ENOENT;

    if (!res_data || !res_data->taskmon_tid_attr) {
        printk(KERN_ERR "remove_tid_file: Invalid reservation data or taskmon_tid_attr for PID %d\n", task->pid);
        return -EINVAL; // Task does not have an associated kobject.
    }

    mutex_lock(&tid_attr_list_mutex);

    // Traverse the tid_attr_list to find the matching node
    node = tid_attr_list;
    while (node) {
        if (node->attr == res_data->taskmon_tid_attr) {
            // Remove the sysfs file
            printk(KERN_INFO "remove_tid_file: Found tid_attr_node for PID %d\n", task->pid);

            sysfs_remove_file(util_kobj, &node->attr->attr);
            printk(KERN_INFO "remove_tid_file: Removed sysfs file for PID %d\n", task->pid);

            // Remove node from the list
            if (prev) {
                prev->next = node->next;
            } else {
                tid_attr_list = node->next;
            }

            // Free allocated memory
            kfree(node->attr->attr.name);
            kfree(node->attr);
            kfree(node);

            // Clear the attribute pointer in reservation data
            res_data->taskmon_tid_attr = NULL;

            ret = 0; // Success
            printk(KERN_INFO "remove_tid_file: Successfully removed sysfs file for PID %d\n", task->pid);
            break;
        }
        prev = node;
        node = node->next;
    }

    if (ret != 0) {
        printk(KERN_ERR "remove_tid_file: No matching tid_attr_node found for PID %d\n", task->pid);
    }

    mutex_unlock(&tid_attr_list_mutex);

    return ret;
}



// Clean up utilization data for a task
void cleanup_utilization_data(struct task_struct *task)
{
    struct reservation_data *res_data = task->reservation_data;
    unsigned long flags;
    struct data_point *point, *tmp;
     // Get reservation data
    res_data = task->reservation_data;
    if (!res_data) {
        printk(KERN_ERR "cleanup_utilization_data: No reservation data for task\n");
        return;
    }

    spin_lock_irqsave(&res_data->data_lock, flags);

    list_for_each_entry_safe(point, tmp, &res_data->data_points, list) {
        list_del(&point->list);
        kfree(point);
    }

    INIT_LIST_HEAD(&res_data->data_points);
    spin_unlock_irqrestore(&res_data->data_lock, flags);

    printk(KERN_INFO "cleanup_utilization_data: Cleaned up utilization data for PID %d\n", task->pid);
}

/// Enable monitoring for all tasks with reservations
void enable_monitoring_for_all_tasks(void)
{
    struct task_struct *task;

    read_lock(&tasklist_lock);
    for_each_process(task) {
        if (task->reservation_data && task->reservation_data->has_reservation) {
            task->reservation_data->monitoring_enabled = true;
            spin_lock_init(&task->reservation_data->data_lock);
            INIT_LIST_HEAD(&task->reservation_data->data_points);
            printk(KERN_INFO "Monitoring enabled for PID %d\n", task->pid);
        }
    }
    read_unlock(&tasklist_lock);
}

/// Disable monitoring for all tasks with reservations
void disable_monitoring_for_all_tasks(void)
{
    struct task_struct *task;

    read_lock(&tasklist_lock);
    for_each_process(task) {
        if (task->reservation_data && task->reservation_data->has_reservation) {
            task->reservation_data->monitoring_enabled = false;
            // cleanup_utilization_data(task);
            // printk(KERN_INFO "Monitoring disabled for PID %d. Data cleaned up.\n", task->pid);
        }
    }
    read_unlock(&tasklist_lock);
}


// init function called during kernel startup
static int __init init_taskmon(void)
{
    int ret;
    free_tid_attr_list();
    release_kobjects();

    ret = init_kobjects();
    if (ret != 0) {
        printk(KERN_ERR "Failed to initialize taskmon kobjects\n");
        return ret;
    }

    ret = create_enabled_file();
    if (ret != 0) {
        printk(KERN_ERR "Failed to create enabled file\n");
        return ret;
    }
    printk(KERN_INFO "Taskmon loaded in the kernel\n");
    return 0;
}

core_initcall(init_taskmon);