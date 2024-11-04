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
#include <linux/reservation.h> // For struct data_point



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

static void taskmon_release(struct kobject *kobj)
{
    // Release the kobject
    kobject_put(kobj);
}

static struct kobj_type taskmon_ktype = {
    .release = taskmon_release,
    .sysfs_ops = &kobj_sysfs_ops,
};

// When the user reads the sysfs file
static ssize_t tid_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    // // Return the current value of taskmon_enabled as 0 or 1
    // return sprintf(buf, "%s\n%s\n%s\n", "10 0.5", "14 0.25", "18 0.25");
    struct task_struct *task;
    struct data_point *point;
    ssize_t len = 0;
    ssize_t temp_len;
    char temp[100];
    pid_t pid;

    // Get the PID from the kobject's name
    if (kstrtoint(kobject_name(kobj), 10, &pid) != 0)
        return -EINVAL;

    // Find the task by PID
    rcu_read_lock();
    task = find_task_by_vpid(pid);
    if (!task) {
        rcu_read_unlock();
        return -ESRCH;
    }
    get_task_struct(task);
    rcu_read_unlock();

    mutex_lock(&task->data_mutex);
    list_for_each_entry(point, &task->data_points, list)
    {
        temp_len = snprintf(temp, sizeof(temp), "%lld.%09ld %llu\n",
                    (long long)point->timestamp.tv_sec, point->timestamp.tv_nsec,
                    point->utilization);

        if (len + temp_len > PAGE_SIZE)
            break;
        memcpy(buf + len, temp, temp_len);
        len += temp_len;
    }
    mutex_unlock(&task->data_mutex);
    put_task_struct(task);
    return len;
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
    mutex_lock(&tid_attr_list_mutex);
    struct tid_attr_node *node = tid_attr_list;
    struct tid_attr_node *next;

    while (node)
    {
        next = node->next;
        sysfs_remove_file((task->taskmon_kobj, &node->attr->attr);)
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

// Create /sys/rtes/taskmon/util/<tid>
int create_tid_file(struct task_struct *task)
{
    int ret;
    struct kobj_attribute *tid_attr;
    struct tid_attr_node *new_node;
    char tid_str[10];

    // Allocate memory for the attribute
    tid_attr = kzalloc(sizeof(*tid_attr), GFP_KERNEL); // alloc and init to zero
    if (!tid_attr)
        return -ENOMEM;

    // Initialize the attribute
    snprintf(tid_str, sizeof(tid_str), "%d", task->pid);
    // Initialize the attribute
    tid_attr->attr.name = kstrdup(tid_str, GFP_KERNEL);
    tid_attr->attr.mode = 0444; // Read-only
    tid_attr->show = tid_show;
    tid_attr->store = NULL;

    // Create the sysfs file
    ret = sysfs_create_file(task->taskmon_kobj, &tid_attr->attr);
    if (ret)
    {
        printk(KERN_ERR "Failed to create file: /sys/rtes/taskmon/util/%d\n", task->pid);
        kobject_put(task->taskmon_kobj);
        kfree(tid_attr->attr.name);
        kfree(tid_attr);
        return ret;
    }
    // Store the kobject in the task struct
    task->taskmon_kobj = kobject_create_and_add(tid_str, util_kobj);
    if (!task->taskmon_kobj)
    {
        printk(KERN_ERR "Failed to create kobject for task %d\n", task->pid);
        kfree(tid_attr->attr.name);
        kfree(tid_attr);
        return -ENOMEM;
    }

    task->taskmon_kobj->ktype = &taskmon_ktype;
    kobject_set_name(task->taskmon_kobj, "%d", task->pid);

    // Add attribute to the list
    new_node = kzalloc(sizeof(*new_node), GFP_KERNEL);
    if (!new_node)
    {
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

    printk(KERN_INFO "Created file: /sys/rtes/taskmon/util/%d\n", task->pid);
    return 0; // Success
}

// Clean up utilization data for a task
void cleanup_utilization_data(struct task_struct *task)
{
    struct data_point *point, *tmp;

    mutex_lock(&task->data_mutex);

    list_for_each_entry_safe(point, tmp, &task->data_points, list) {
        list_del(&point->list);
        kfree(point);
    }

    mutex_unlock(&task->data_mutex);
}

/// Enable monitoring for all tasks with reservations
void enable_monitoring_for_all_tasks(void)
{
    struct task_struct *task;

    read_lock(&tasklist_lock);
    for_each_process(task) {
        if (task->has_reservation) {
            task->monitoring_enabled = true;
            mutex_init(&task->data_mutex);
            INIT_LIST_HEAD(&task->data_points);
            create_tid_file(task);
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
        if (task->has_reservation) {
            task->monitoring_enabled = false;
            if (task->taskmon_kobj) {
                sysfs_remove_file(task->taskmon_kobj, &enabled_attr.attr);
                kobject_put(task->taskmon_kobj);
                task->taskmon_kobj = NULL;
            }
            cleanup_utilization_data(task);
        }
    }
    read_unlock(&tasklist_lock);

    // Clean up the tid attribute list
    free_tid_attr_list();
}

// init function called during kernel startup
void __init init_taskmon(void)
{
    int ret;

    ret = init_kobjects();
    if (ret != 0) {
        printk(KERN_ERR "Failed to initialize taskmon kobjects\n");
        return;
    }

    ret = create_enabled_file();
    if (ret != 0) {
        printk(KERN_ERR "Failed to create enabled file\n");
        return;
    }
}

void cleanup_taskmon(void)
{
    disable_monitoring_for_all_tasks();
    release_kobjects();
}

EXPORT_SYMBOL(taskmon_enabled);
EXPORT_SYMBOL(init_taskmon);
EXPORT_SYMBOL(create_tid_file);
EXPORT_SYMBOL(cleanup_utilization_data);
EXPORT_SYMBOL(enable_monitoring_for_all_tasks);
EXPORT_SYMBOL(disable_monitoring_for_all_tasks);
