/**
 * 2.2.2 Reserve management syscalls
 * Implement two custom syscalls
 * 
 * set_reserve (change the reservation parameters on a thread with an existing reservation)
 * set a reservation will associate the C and the T value with the thread, 
 * pin the thread to the given core, and initialize the timers you need.
 * 
 * & cancel_reserve
 * cancel a reservation will remove the thread from the watchful eyes of your
 * reservation framework and perform cleanup.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/cpumask.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/slab.h> 
#include <linux/signal.h>
#include <linux/list.h>
#include <linux/math64.h>
#include <linux/ktime.h>
#include <linux/reservation.h>
#include <linux/sysfs.h>
#include <linux/spinlock.h>
#include "taskmon.h"

// List to store reserved tasks
static LIST_HEAD(reserved_tasks_list);
// Mutex for tasks list
static spinlock_t reserved_tasks_list_lock;
// Node in the list
struct task_node {
    struct task_struct *task; // Pointer to task_struct
    struct list_head list;    // List head for linking
};

void add_task_to_list(struct task_struct *task) {
    struct task_node *new_node;

    new_node = kmalloc(sizeof(*new_node), GFP_KERNEL);
    if (!new_node) {
        printk(KERN_ERR "add_task_to_list: Failed to allocate memory for task_node\n");
        return;
    }

    new_node->task = task;
    INIT_LIST_HEAD(&new_node->list);

    spin_lock(&reserved_tasks_list_lock); // Acquire the lock
    list_add_tail(&new_node->list, &reserved_tasks_list);
    spin_unlock(&reserved_tasks_list_lock); // Release the lock
}
void remove_task_from_list(struct task_struct *task) {
    struct task_node *node, *tmp;

    spin_lock(&reserved_tasks_list_lock); // Acquire the lock
    list_for_each_entry_safe(node, tmp, &reserved_tasks_list, list) {
        if (node->task == task) {
            list_del(&node->list);
            spin_unlock(&reserved_tasks_list_lock); // Release before freeing
            kfree(node);
            return;
        }
    }
    spin_unlock(&reserved_tasks_list_lock); // Release the lock
    printk(KERN_ERR "remove_task_from_list: Task not found in the list\n");
}

struct reservation_data *create_reservation_data(struct task_struct *task) {
    struct reservation_data *res_data;

    // Allocate memory for the reservation data
    res_data = kmalloc(sizeof(struct reservation_data), GFP_KERNEL);
    if (!res_data)
        return NULL;

    // Initialize all fields in reservation data
    memset(res_data, 0, sizeof(struct reservation_data));
    INIT_LIST_HEAD(&res_data->data_points);
    spin_lock_init(&res_data->data_lock);
    res_data->taskmon_kobj = NULL;
    res_data->has_reservation = false;
    res_data->monitoring_enabled = false;

    // Link reservation data to the task
    task->reservation_data = res_data;

    return res_data;
}

/** called periodically by the high-resolution timer with eachh task having an active reservation
 *  1. check if the task has overrun its budget
 *  2. send SIGEXCESS signal to the task if overrun detected
 *  3. collect utilization data if monitoring is enabled, store the data in the task's data_points list
 * 
*/
enum hrtimer_restart reservation_timer_callback(struct hrtimer *timer) {
    struct reservation_data *res_data = container_of(timer, struct reservation_data, reservation_timer);
    struct task_struct *task = res_data->task;  // Get the associated task
    struct data_point *point;
    unsigned long flags;
    u64 exec_ns, period_ns, utilization_integer;
    u32 utilization_fraction, remainder;
    char utilization_str[32];

    exec_ns = res_data->exec_accumulated_time;
    period_ns = timespec_to_ns(&res_data->reserve_T);

    res_data->period_count++;  // Increment the period count
    utilization_integer = div_u64_rem(exec_ns * 100, (u32)period_ns, &remainder);
    utilization_fraction = div_u64_rem((u64)remainder * 100, (u32)period_ns, &remainder);

    // Format the result as a floating-point style string "0.xx"
    snprintf(utilization_str, sizeof(utilization_str), "0.%02llu", utilization_integer);

    // Print the utilization for debugging purposes
    // printk(KERN_INFO "Utilization: %s\n", utilization_str);
    
    // Collect utilization data if monitoring is enabled
    if (taskmon_enabled && res_data->monitoring_enabled) {
        point = kmalloc(sizeof(*point), GFP_ATOMIC);
        if (point) {
            point->timestamp = div64_u64((u64)res_data->period_count * period_ns, 1000000); // Convert to ms
            
            strncpy(point->utilization, utilization_str, sizeof(point->utilization) - 1);
            point->utilization[sizeof(point->utilization) - 1] = '\0';  //  null-terminator
            spin_lock_irqsave(&res_data->data_lock, flags);
            list_add_tail(&point->list, &res_data->data_points);
            // printk(KERN_INFO "Added data point for PID: %d, timestamp=%llu, utilization=%s\n",
            //        res_data->task->pid, point->timestamp, point->utilization);
            spin_unlock_irqrestore(&res_data->data_lock, flags);
        } else {
            printk(KERN_ERR "Failed to allocate memory for data point\n");
        }
    }

    // New period so reset states:
    res_data->exec_accumulated_time = 0;  // Reset accumulated time
    // Wake up task at new period if it has been suspended
    if (task->state == TASK_UNINTERRUPTIBLE) {
        wake_up_process(task);
    }


    // Hrtimer will end until you restart it again!
    hrtimer_forward_now(timer, ktime_set(0, period_ns)); // Forward timer to next period
    return HRTIMER_RESTART;
}

uint32_t utilization_bound(uint32_t n) {
    uint32_t precomputed_utilization[10] = {
        // Utilization values * 1000
        // n*(2^{1/n} – 1)
        1000, 828, 780, 757, 743, 735, 729, 724, 721, 718
    };
    if (n <= 10) {
        return precomputed_utilization[n - 1];
    } else {
        return 693; // infinity value
    }
}

uint32_t div_C_T(uint32_t C, uint32_t T)
{
    uint32_t result_C_T, dividend;
    // Compute C/T using do_div
    dividend = (uint64_t)C * 1000; // Scale C to fixed-point
    // do_div returns in quotient in dividend and remainder in output
    do_div(dividend, T);             // Divide by T
    result_C_T = dividend;                       // Quotient in fixed-point
    return result_C_T;
}

/**
 * Check if new task can be schedulable based on UB and then RT tests
 * @param cpuid cpu id to check schedulability
 * @param c computation time of new task to be added
 * @param t period of new task to be added
 */
int check_schedulability(int cpuid, struct timespec c, struct timespec t) {
    // Utilization Bound (UB) Test
    
    // n = number of tasks
    uint32_t UB, C, T, U;
    struct task_node *node;
    struct reservation_data *res_data;
    // Init variables with the newly added task
    int n = 1;
    C = timespec_to_ns(&c);
    T = timespec_to_ns(&t);
    U = div_C_T(C, T); // Scaled "double", initialized to 0


    // Iterate over reserved_tasks_list
    spin_lock(&reserved_tasks_list_lock);
    list_for_each_entry(node, &reserved_tasks_list, list) {
        // Only if on the same cpu, take into account
        if (task_cpu(node->task) == cpuid) {
            res_data = node->task->reservation_data;
            // U = C/T
            U += div_C_T(timespec_to_ns(&res_data->reserve_C), timespec_to_ns(&res_data->reserve_T));
            n++; // Increment number of tasks
        }
    }
    spin_unlock(&reserved_tasks_list_lock);

    // Calculate the Utilization Bound (UB) for the new task
    UB = utilization_bound(n);

    printk(KERN_INFO "n=%d\n", n);
    printk(KERN_INFO "UB: %d, U: %d\n", UB, U);
    // Compare the Utilization Bound (UB) with the Utilization (U)
    if (U > UB) {
        printk(KERN_ERR "check_schedulability: Utilization Bound (UB) test failed\n");
        return -EBUSY; // Utilization Bound (UB) test failed
    }

    return 0; // Success
}


SYSCALL_DEFINE4(set_reserve, pid_t, pid, struct timespec __user *, C, struct timespec __user *, T, int, cpuid) {
    struct task_struct *task;
    struct reservation_data *res_data;
    struct timespec c, t;
    int ret;
    cpumask_t cpumask;

    // ensure cpuid is valid
    if (cpuid < 0 || cpuid > 3) {
        return -EINVAL;
    }

    // copy reservation params from user space
    if(copy_from_user(&c, C, sizeof(struct timespec)) || copy_from_user(&t, T, sizeof(struct timespec))) {
        return -EFAULT;
    }

    // ensure non-negative c and t
    if (c.tv_sec < 0 || t.tv_sec < 0 || c.tv_nsec < 0 || t.tv_nsec < 0) {
        return -EINVAL;
    }

    // Check schedulability before adding
    if (check_schedulability(cpuid, c, t) < 0){
        return EBUSY;
    }

    // retrieve the task struct
    if (pid == 0) {
        task = current;
    } else {
        rcu_read_lock();
        task = find_task_by_vpid(pid);
        if(!task) {
            rcu_read_unlock();
            return -ESRCH; //no such process
        }
        get_task_struct(task);      // increase the ref count to prevent task being freed
        rcu_read_unlock();
    }

    if (!task->reservation_data) {
        res_data = create_reservation_data(task);
        if (!res_data) {
            if (pid != 0) {
                put_task_struct(task);
            }
            return -ENOMEM;
        }
    } else {
        res_data = task->reservation_data;
        hrtimer_cancel(&res_data->reservation_timer);  // Cancel existing timer if present
    }

    // inti monitoring data
    res_data->reserve_C = c;
    res_data->reserve_T = t;
    res_data->has_reservation = true;
    res_data->task = task;
    res_data->monitoring_enabled = taskmon_enabled;

    // Create sysfs file regardless of taskmon_enabled
    if (!res_data->taskmon_tid_attr) {
        ret = create_tid_file(task);
        if (ret) {
            printk(KERN_ERR "set_reserve: Failed to create tid file for PID %d with error %d\n", task->pid, ret);
            res_data->has_reservation = false;
            if (pid != 0) {
                put_task_struct(task);
            }
            return ret;
        }
    }

    res_data->exec_accumulated_time = 0;
    getrawmonotonic(&(res_data->exec_start_time)); // Init exec start time to now
  
    // set specified cpu
    cpumask_clear(&cpumask);
    cpumask_set_cpu(cpuid, &cpumask);
    ret = set_cpus_allowed_ptr(task, &cpumask); // user kernel space func to set task's cpu affinity
    if (ret) {
        printk(KERN_ERR "Failed to set CPU affinity for PID %d\n", task->pid);
        if (pid != 0) {
            put_task_struct(task);  //decrease the ref count
        }
        return ret;
    }
    
    // initialize a high resolution timer trigger periodically T units
    hrtimer_init(&res_data->reservation_timer, CLOCK_REALTIME, HRTIMER_MODE_REL);
    res_data->reservation_timer.function = reservation_timer_callback;
    res_data->task = task;  // Link task to reservation data for callback
    hrtimer_start(&res_data->reservation_timer, ktime_set(0, timespec_to_ns(&t)), HRTIMER_MODE_REL);
    

    if (pid != 0) 
        put_task_struct(task);

    // Add to the reserved tasks list
    add_task_to_list(task);

    printk(KERN_INFO "set_reserve called: pid=%d, C=%ld.%09ld, T=%ld.%09ld, cpuid=%d\n",
           pid, c.tv_sec, c.tv_nsec, t.tv_sec, t.tv_nsec, cpuid);

    return 0;
}


//remove the thread
SYSCALL_DEFINE1(cancel_reserve, pid_t, pid) {
    struct task_struct *task;
    struct reservation_data *res_data;
    // retrieve the task
    if (pid == 0) {
        task = current;
    } else {
        rcu_read_lock();
        task = find_task_by_vpid(pid);
        if (!task) {
            rcu_read_unlock();
            return -ESRCH;
        }
        get_task_struct(task);
        rcu_read_unlock();
    }

    // check if the reservation exist
    res_data = task->reservation_data;
    if (!res_data || !res_data->has_reservation) {
        if (pid != 0) {
            put_task_struct(task);
        }
        return -EINVAL;
    }

    // cancel hrtimer
    hrtimer_cancel(&res_data->reservation_timer);

    // clear up reservation parameters
    res_data->reserve_C = (struct timespec){0, 0};
    res_data->reserve_T = (struct timespec){0, 0};
    
    set_cpus_allowed_ptr(task, cpu_all_mask);
    res_data->has_reservation = false;
    remove_tid_file(task);
    cleanup_utilization_data(task);
    
    if (pid != 0) 
        put_task_struct(task);

    // Remove task from the reserved tasks list
    remove_task_from_list(task);

    printk(KERN_INFO "cancel_reserve: Reservation cancelled for PID %d\n", task->pid);
    return 0;
}

/**
 * Provide a system call end_job(), which suspends the calling thread until the beginning 
 * of its next period, at which point it should be scheduled normally according to its 
 * reservation. Any unused budget does not get carried over into the next period. 
 * While the thread is suspended, signals should not wake it up.
 */
SYSCALL_DEFINE0(end_job) {
    // `current` is a global pointer to the task_struct of the currently running process
    // Check that task must have reservation or else do nothing
    if (current->reservation_data && !(current->reservation_data->has_reservation)) {
        // Does not have reservation, return error
        printk(KERN_ERR "end_job: No reservation for PID %d, aborting!\n", current->pid);
        return -2; // Let -2 mean no reservation
    }

    printk(KERN_INFO "end_job: Suspended PID %d\n", current->pid);

    // Set task state to TASK_UNINTERRUPTIBLE
    set_current_state(TASK_UNINTERRUPTIBLE);
    // Force a reschedule to suspend the current task
    schedule();

    return 0; // Return success
}

// When the user reads the /sys/rtes/reserves file
static ssize_t reserves_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{    
    /**
     * Reservation status 
     *   Create a virtual sysfs file at /sys/rtes/reserves whose dynamic contents is a list of threads with active
     *   reserves. For each thread display its thread ID, the process ID, real-time priority, command name, and the
     *   CPU ID to which the thread is pinned in the following format:
     *   TID PID PRIO CPU NAME
     *   101 101 99 2 adb
     *   1568 1568 0 0 periodic
     */
    // Print values from the reserved_tasks_list in the required format
    struct task_node *node;
    struct task_struct *task;
    int len = 0;

    // Table header
    len += sprintf(buf + len, " TID  PID PRIO CPU NAME\n");

    spin_lock(&reserved_tasks_list_lock); // Acquire the lock
    list_for_each_entry(node, &reserved_tasks_list, list) {
        task = node->task;
        // TID PID PRIO CPU NAME
        len += sprintf(buf + len, "%4d %4d %4d %3d %s\n", task->pid, task->tgid, task->rt_priority, task_cpu(task), task->comm);
    }
    spin_unlock(&reserved_tasks_list_lock); // Release the lock

    return len;
}

/** Initializes the kobj_attribute struct with the reserves_show function
 *   - reserves is the name of the sysfs file
 *   - 0444 is the permissions of the sysfs file, read only for all users
 *   - reserves_show is the function to call when the user reads the sysfs file
 */
static struct kobj_attribute reserves_attr = __ATTR(reserves, 0444, reserves_show, NULL);

// Create the sysfs file /sys/rtes/reserves
int create_reserves_file(void)
{
    int ret;

    if (rtes_kobj == NULL){
        printk(KERN_ERR "reserve: critical error - rtes_obj is not initialized yet!\n");
        return -1;
    }

    // Create a sysfs file named "reserves" under the "rtes" kobject
    ret = sysfs_create_file(rtes_kobj, &reserves_attr.attr);
    if (ret)
    {
        printk(KERN_ERR "Failed to create file: /sys/rtes/reserves\n");
        kobject_put(rtes_kobj);
        return -1;
    }
    printk(KERN_INFO "Created file: /sys/rtes/reserves\n");
    return 0; // Success
}

/**
 * Reservation status
 *   > Create a virtual sysfs file at /sys/rtes/reserves whose dynamic contents is a list of threads with active
 *   > reserves. For each thread display its thread ID, the process ID, real-time priority, command name, and the
 *   > CPU ID to which the thread is pinned in the following format:
 *   > TID PID PRIO CPU NAME
 *   > 101 101 99   2   adb
 */
static int __init init_reserve(void)
{
    int ret;

    ret = create_reserves_file();
    if (ret != 0) {
        printk(KERN_ERR "Failed to create reserves file\n");
        return ret;
    }

    // Initialize the spinlock for the reserved tasks list
    spin_lock_init(&reserved_tasks_list_lock);

    return 0; // Success
}

// Use postcore so that it runs after rtes_kobj is initialized by taskmon
postcore_initcall(init_reserve);
