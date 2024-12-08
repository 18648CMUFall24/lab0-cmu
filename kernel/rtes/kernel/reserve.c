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

enum partition_policy current_policy = FF; // Default policy is First Fit
struct bucket_info processors[MAX_PROCESSORS];

static const char *policy_names[] = {
    [FF] = "FF",
    [NF] = "NF",
    [BF] = "BF",
    [WF] = "WF",
    [LST] = "LST"
};


static spinlock_t policy_lock;
static spinlock_t processors_lock;
static spinlock_t reserved_tasks_list_lock;
static DEFINE_MUTEX(bin_packing_mutex);


uint32_t div_C_T(uint32_t C, uint32_t T)
{
    uint64_t result_C_T, dividend;
    // Compute C/T using do_div
    dividend = (uint64_t)C * 1000; // Scale C to fixed-point
    // do_div returns in quotient in dividend and remainder in output
    result_C_T = div64_s64(dividend, (uint64_t)T);
    return result_C_T;
}


void initialize_processors(void) {
    int i;
    for (i = 0; i < MAX_PROCESSORS; i++) {
        processors[i].running_util = 0;
        processors[i].num_tasks = 0;
        processors[i].first_task = NULL;
    }
}

// List to store reserved tasks
static LIST_HEAD(reserved_tasks_list);

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


bool turn_on_processor(int cpu) {
    if (cpu_online(cpu)) {
        printk(KERN_INFO "Processor %d is already online\n", cpu);
        return true; 
    }

    printk(KERN_INFO "Turning on processor %d\n", cpu);
    if (!cpu_up(cpu)) {
        printk(KERN_INFO "Processor %d successfully turned on\n", cpu);
        return true; 
    } else {
        printk(KERN_ERR "Failed to turn on processor %d\n", cpu);
        return false; 
    }
}


void turn_off_unused_processors(void) {
    int i;
    int online_cpus = 0;

    // Count the number of currently online CPUs
    for (i = 0; i < MAX_PROCESSORS; i++) {
        if (cpu_online(i)) {
            online_cpus++;
        }
    }

    // Turn off unused processors while keeping at least one online
    for (i = 0; i < MAX_PROCESSORS; i++) {
        if (processors[i].num_tasks == 0 && cpu_online(i)) {
            if (online_cpus > 1) { // Ensure at least one CPU remains online
                if (cpu_down(i) == 0) {
                    printk(KERN_INFO "Processor %d successfully turned off\n", i);
                    online_cpus--;
                } else {
                    printk(KERN_ERR "Failed to turn off processor %d\n", i);
                }
            } else {
                printk(KERN_INFO "Processor %d remains online to ensure at least one active CPU\n", i);
            }
        }
    }
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


static int response_time_test(struct timespec *c, struct timespec *t, int task_idx, int total_tasks, struct timespec *c_list, struct timespec *t_list) {
    uint64_t R_prev, R_curr, T_i, C_i, C_j, T_j, interference;
    int iteration, j;

    // task under test, start rt test when that task begins to need more than UB
    C_i = timespec_to_ns(&c_list[task_idx]);
    T_i = timespec_to_ns(&t_list[task_idx]);

    printk(KERN_INFO "Starting RT Test for Task.\n");
    // response time R_0 = C_1 + C_2 + ... + C_i
    R_prev = 0;
    for (j = 0; j <= task_idx; j++) {
        R_prev += timespec_to_ns(&c_list[j]);
    }

    // iteratively calculate response time , use 50 as max iterations
    for (iteration = 0; iteration < 50; iteration++) {
        interference = 0;

        for (j = 0; j < task_idx; j++) {
            T_j = timespec_to_ns(&t_list[j]);
            C_j = timespec_to_ns(&c_list[j]);
            interference += div64_u64((R_prev + T_j - 1), T_j) * C_j; // ceil(R_prev/T_j) * C_j
        }

        // Calculate new response time
        R_curr = C_i + interference;

        // Check if response time converges
        if (R_curr == R_prev) {
            if (R_curr <= T_i) {
                return 0; // schedulable
            } else {
                
                return -EBUSY; // not schedulable
            }
        }

        R_prev = R_curr;
        if (iteration == 49) {
            printk(KERN_ERR "RT test did not converge for task %d.\n", task_idx);
            return -EBUSY;
        }
    }

    printk(KERN_ERR "Task %d failed RT test with response time R_curr=%llu and T_i=%llu.\n", task_idx, R_curr, T_i);
    return -EBUSY; 
}

/**
 * Check if new task can be schedulable based on UB and then RT tests
 * @param cpuid cpu id to check schedulability
 * @param c computation time of new task to be added
 * @param t period of new task to be added
 */
int check_schedulability(int cpuid, struct timespec c, struct timespec t) {
    // Utilization Bound (UB) Test
    uint32_t UB, C_i, T_i, U = 0;
    struct task_node *node;
    struct reservation_data *res_data;
    struct timespec c_list[MAX_TASKS], t_list[MAX_TASKS];
    // Init variables with the newly added task
    int num_task = 0;
    int i, j, k;

    c_list[num_task] = c;
    t_list[num_task] = t;
    num_task++;

    // get the a list for each task on the same cpu
    spin_lock(&reserved_tasks_list_lock);
    list_for_each_entry(node, &reserved_tasks_list, list) {
        // Only if on the same cpu, take into account
        if (task_cpu(node->task) == cpuid) {
            res_data = node->task->reservation_data;
            c_list[num_task] = res_data->reserve_C;
            t_list[num_task] = res_data->reserve_T;
            num_task++;
        }
    }
    spin_unlock(&reserved_tasks_list_lock);

    // bubble sort the tasks based on period in ascending order
    for (i = 0; i < num_task - 1; i++) {
        for (j = i + 1; j < num_task; j++) {
            if (timespec_to_ns(&t_list[i]) > timespec_to_ns(&t_list[j])) {
                struct timespec tmp_c = c_list[i], tmp_t = t_list[i];
                c_list[i] = c_list[j];
                t_list[i] = t_list[j];
                c_list[j] = tmp_c;
                t_list[j] = tmp_t;
            }
        }
    }

     // Perform UB Test and RT Test if necessary
    for (i = 0; i < num_task; i++) {
        C_i = timespec_to_ns(&c_list[i]);
        T_i = timespec_to_ns(&t_list[i]);
        U += div_C_T(C_i, T_i);

        UB = utilization_bound(i + 1);
        printk(KERN_INFO "UB Test for cpu%d: Task %d, U=%u, UB=%u.\n", cpuid, i + 1, U, UB);

        if (U > UB) {
            // UB test fails; perform RT test
            printk(KERN_INFO "UB test failed at task %d. Running RT test.\n", i+1);

            if (num_task >= MAX_TASKS) {
                printk(KERN_ERR "Exceeded MAX_TASKS for schedulability check.\n");
                return -ENOMEM;
            }

            if (response_time_test(c_list, t_list, i, num_task, c_list, t_list) != 0) {
                printk(KERN_ERR "Task %d failed RT test. Not schedulable.\n", i);
                return -EBUSY;
            }

            // continue testing subsequent tasks
            for (k = i + 1; k < num_task; k++) {
                if (response_time_test(c_list, t_list, k, num_task, c_list, t_list) != 0) {
                    printk(KERN_ERR "Task %d failed RT test. Not schedulable.\n", k);
                    return -EBUSY; 
                }
            }

            break; 
        }
    }
    printk(KERN_INFO "Task schedulable on CPU %d\n", cpuid);
    return 0; // Success
}


int find_best_processor(uint32_t util, enum partition_policy policy, struct timespec C, struct timespec T) {
    int best_processor = -1;
    int i;
    static int last_processor = 0;
    uint32_t remaining_space, min_space_left = 1001, max_space_left = 0; // ala  min space over 100% utilization
    uint32_t min_util = 1001;

    printk(KERN_INFO "Finding best processor for task with util=%u, policy=%s\n", util, policy_names[policy]);
    switch (policy) {
        case FF: // First-Fit
            for (i = 0; i < MAX_PROCESSORS; i++) {
                if (processors[i].running_util + util <= 1000) {
                    // printk(KERN_INFO "First-Fit: Processor %d selected, running_util=%u, util=%u\n",
                    //        i, processors[i].running_util, util);
                    // return i;
                    if (check_schedulability(i, C, T) == 0) {
                        printk(KERN_INFO "First-Fit: Processor %d selected, running_util=%u, util=%u\n",
                           i, processors[i].running_util, util);
                        return i;
                    }
                }
            }
            break;

        case NF: // Next-Fit
            for (i = 0; i < MAX_PROCESSORS; i++) {
                int idx = (last_processor + i) % MAX_PROCESSORS;    // Start from the last processor
                if (processors[idx].running_util + util <= 1000) {

                    // last_processor = idx;
                    // printk(KERN_INFO "Next-Fit: Processor %d selected, running_util=%u, util=%u\n",
                    //        idx, processors[idx].running_util, util);
                    // return idx;
                    if (check_schedulability(idx, C, T) == 0) {
                        last_processor = idx;
                        printk(KERN_INFO "Next-Fit: Processor %d selected, running_util=%u, util=%u\n",
                           idx, processors[idx].running_util, util);
                        return idx;
                    }
                }
            }
            break;

        case BF: // Best-Fit
            for (i = 0; i < MAX_PROCESSORS; i++) {
                remaining_space = 1000 - processors[i].running_util;
                if (remaining_space >= util) {
                    if (remaining_space < min_space_left) {
                            min_space_left = remaining_space;
                            best_processor = i;
                    }
                    // if (check_schedulability(i, C, T) == 0) {
                    //     if (remaining_space < min_space_left) {
                    //         min_space_left = remaining_space;
                    //         best_processor = i;
                    //     }
                    // }
                }
            }

            if (best_processor != -1) {
                printk(KERN_INFO "Best-Fit: Processor %d selected, running_util=%u, util=%u\n",
                    best_processor, processors[best_processor].running_util, util);
            } else {
                printk(KERN_ERR "Best-Fit: No suitable processor found for util=%u\n", util);
            }

            return best_processor;

        case WF: // Worst-Fit
            for (i = 0; i < MAX_PROCESSORS; i++) {
                remaining_space = 1000 - processors[i].running_util;
                if (remaining_space >= util) {
                    if (remaining_space > max_space_left) {
                            max_space_left = remaining_space;
                            best_processor = i;
                    }
                    // if (check_schedulability(i, C, T) == 0) {
                    //     if (remaining_space > max_space_left) {
                    //         max_space_left = remaining_space;
                    //         best_processor = i;
                    //     }
                    // }
                }
            }

            if (best_processor != -1) {
                printk(KERN_INFO "Worst-Fit: Processor %d selected, running_util=%u, util=%u\n",
                       best_processor, processors[best_processor].running_util, util);
            } else {
                printk(KERN_ERR "Worst-Fit: No suitable processor found for util=%u\n", util);
            }

            return best_processor;
        case LST:
            for (i = 0; i < MAX_PROCESSORS; i++) {
                if (processors[i].running_util + util < 1001) {
                    if (check_schedulability(i, C, T) == 0) {
                        if (processors[i].running_util < min_util) {
                            min_util = processors[i].running_util;
                            best_processor = i;
                        }
                    }
                }
            }

            if (best_processor != -1) {
                printk(KERN_INFO "List-Scheduling: Processor %d selected, running_util=%u, util=%u\n",
                    best_processor, processors[best_processor].running_util, util);
            } else {
                printk(KERN_ERR "List-Scheduling: No suitable processor found for util=%u\n", util);
            }

            return best_processor;

        default:
            printk(KERN_ERR "Unknown partitioning policy.\n");
            return -1;
    }

    printk(KERN_INFO "No suitable processor found for task with util=%u\n", util);
    return -1; // No processor can accommodate the task

}

void print_processor_info(int cpuid) {
    struct bucket_task_ll *curr;
    int task_count = 0;

    printk(KERN_INFO "Processor %d Info:", cpuid);
    printk(KERN_INFO "  Running Utilization: %u", processors[cpuid].running_util);
    printk(KERN_INFO "  Number of Tasks: %d", processors[cpuid].num_tasks);

    curr = processors[cpuid].first_task;
    while (curr) {
        printk(KERN_INFO "    Task %d: Util=%u, Cost=%lu.%09lu, Period=%lu.%09lu",
               curr->task->pid, curr->util,
               curr->cost.tv_sec, curr->cost.tv_nsec,
               curr->period.tv_sec, curr->period.tv_nsec);
        curr = curr->next;
        task_count++;
    }

    if (task_count != processors[cpuid].num_tasks) {
        printk(KERN_ERR "Processor %d task count mismatch! Expected %d, Found %d",
               cpuid, processors[cpuid].num_tasks, task_count);
    }
}

void add_task_to_processor(struct task_struct *task, struct timespec C, struct timespec T, int cpuid) {
    struct bucket_task_ll *new_task;
    uint32_t util = div_C_T(timespec_to_ns(&C), timespec_to_ns(&T));
    // int i;

    turn_on_processor(cpuid); // Ensure the processor is online

    new_task = kmalloc(sizeof(*new_task), GFP_KERNEL);

    new_task->task = task;
    new_task->util = util;
    new_task->cost = C;
    new_task->period = T;
    new_task->next = processors[cpuid].first_task;
    processors[cpuid].first_task = new_task;

    processors[cpuid].running_util += util;
    processors[cpuid].num_tasks++;

    printk(KERN_INFO "Task %d added to processor %d. Utilization: %u\n", task->pid, cpuid, util);
    print_processor_info(0);
    print_processor_info(1);
    print_processor_info(2);
    print_processor_info(3);
}

// Remove task from processor bucket
void remove_task_from_processor(struct task_struct *task) {
    int i;
    struct bucket_task_ll *curr, *prev;
    for (i = 0; i < MAX_PROCESSORS; i++) {
        spin_lock(&processors_lock); // Protect access to processors array
        prev = NULL;
        curr = processors[i].first_task;
        printk(KERN_DEBUG "Checking processor %d for task %d\n", i, task->pid);

        while (curr) {
            if (curr->task == task) {
                // Update the linked list
                if (prev) {
                    prev->next = curr->next;
                } else {
                    processors[i].first_task = curr->next;
                }

                // Update processor utilization and task count
                processors[i].running_util -= curr->util;
                processors[i].num_tasks--;
                kfree(curr);
                printk(KERN_INFO "Task %d removed from processor %d\n", task->pid, i);
                spin_unlock(&processors_lock);

                // Check if processor is now unused and turn it off
                if (processors[i].num_tasks == 0) {
                    printk(KERN_INFO "Processor %d is now unused. Attempting to turn off.\n", i);
                    if (i != 0 && cpu_online(i)) { // Keep processor 0 always online
                        if (cpu_down(i)) {
                            printk(KERN_ERR "Failed to bring processor %d offline\n", i);
                        } else {
                            printk(KERN_INFO "Processor %d brought offline\n", i);
                        }
                    }
                }
                // turn_off_unused_processors()
                print_processor_info(0);
                print_processor_info(1);
                print_processor_info(2);
                print_processor_info(3);
                return;
            }
            prev = curr;
            curr = curr->next;
        }
        spin_unlock(&processors_lock);
    }
    printk(KERN_ERR "Task %d not found in any processor bucket\n", task->pid);
}

SYSCALL_DEFINE4(set_reserve, pid_t, pid, struct timespec __user *, C, struct timespec __user *, T, int, cpuid) {
    struct task_struct *task;
    struct reservation_data *res_data;
    struct timespec c, t;
    int ret, processor_id;
    cpumask_t cpumask;
    uint32_t util;

    // ensure cpuid is valid
    if (cpuid < -1 || cpuid > 3) {
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
    
    // util (1000)
    util = div_C_T(timespec_to_ns(&c), timespec_to_ns(&t));


    if (cpuid == -1) { 
        // Handle bin-packing case
        mutex_lock(&bin_packing_mutex);
        processor_id = find_best_processor(util, current_policy, c, t); 
        if (processor_id < 0) {
            // spin_unlock(&processors_lock);
            printk(KERN_ERR "Task %d cannot be assigned to any processor.\n", pid);
            mutex_unlock(&bin_packing_mutex);
            return -EBUSY;
        }
        // if (processor_id >= 0) {
        //     add_task_to_processor(task, C, T, processor_id);
        // }
        mutex_unlock(&bin_packing_mutex);
        printk(KERN_INFO "Bin packing: Task %d assigned to processor %d\n", pid, processor_id);
    } else if (cpuid < -1|| cpuid >= MAX_PROCESSORS) {
        // spin_unlock(&processors_lock);
        return -EINVAL; // Invalid CPU ID
    } else {
        processor_id = cpuid; // Single processor specified
        spin_lock(&processors_lock);
        // Check schedulability before adding
        if (check_schedulability(processor_id, c, t) < 0){
            spin_unlock(&processors_lock);
            printk(KERN_ERR "Task %d cannot be assigned to processor %d.\n", pid, processor_id);
            return -EBUSY;
        }
        spin_unlock(&processors_lock);
    }
    // spin_lock(&processors_lock);
    // // Check schedulability before adding
    // if (check_schedulability(processor_id, c, t) < 0){
    //     spin_unlock(&processors_lock);
    //     printk(KERN_ERR "Task %d cannot be assigned to processor %d.\n", pid, processor_id);
    //     return -EBUSY;
    // }
    // spin_unlock(&processors_lock);


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
    cpumask_set_cpu(processor_id, &cpumask);
    ret = set_cpus_allowed_ptr(task, &cpumask); // user kernel space func to set task's cpu affinity
    if (ret) {
        printk(KERN_ERR "Failed to set CPU affinity for PID %d\n", task->pid);
        if (pid != 0) {
            put_task_struct(task);  //decrease the ref count
        }
        return ret;
    }
    
    // Add task to the processor
    spin_lock(&processors_lock);
    add_task_to_processor(task, c, t, processor_id);
    spin_unlock(&processors_lock);
    
    // initialize a high resolution timer trigger periodically T units
    hrtimer_init(&res_data->reservation_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
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
    if (hrtimer_active(&res_data->reservation_timer)) {
        int ret = hrtimer_cancel(&res_data->reservation_timer);
        if (ret < 0) {
            printk(KERN_WARNING "cancel_reserve: Failed to cancel hrtimer for PID %d\n", task->pid);
        }
    }

    // clear up reservation parameters
    res_data->reserve_C = (struct timespec){0, 0};
    res_data->reserve_T = (struct timespec){0, 0};
    
    set_cpus_allowed_ptr(task, cpu_all_mask);
    res_data->has_reservation = false;
    remove_tid_file(task);
    cleanup_utilization_data(task);
    
    if (pid != 0) 
        put_task_struct(task);

    // Remove task from the processor
    remove_task_from_processor(task);
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

static ssize_t partition_policy_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    ssize_t len;
    spin_lock(&policy_lock);
    len = sprintf(buf, "%s\n", policy_names[current_policy]);
    spin_unlock(&policy_lock);
    return len;
}

static ssize_t partition_policy_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    enum partition_policy new_policy;
    char input[16]; // Enough for policy names (FF, NF, BF, WF)
    int i;

    // Ensure no active reservations
    spin_lock(&reserved_tasks_list_lock);
    if (!list_empty(&reserved_tasks_list)) {
        spin_unlock(&reserved_tasks_list_lock);
        printk(KERN_ERR "Cannot change policy: active reservations exist.\n");
        return -EBUSY; // Fail if there are active reservations
    }
    spin_unlock(&reserved_tasks_list_lock);

    // Copy and sanitize input
    if (count > sizeof(input) - 1) {
        return -EINVAL; // Input too long
    }
    strncpy(input, buf, count);
    input[count] = '\0'; // Null-terminate the input

    // Match input to policy names (case-insensitive)
    for (i = 0; i < ARRAY_SIZE(policy_names); i++) {
        if (strncasecmp(input, policy_names[i], strlen(policy_names[i])) == 0) {
            new_policy = i;
            break;
        }
    }

    // If no matching policy found, return an error
    if (i == ARRAY_SIZE(policy_names)) {
        printk(KERN_ERR "Invalid partitioning policy: %s\n", input);
        return -EINVAL;
    }

    // Update the policy
    spin_lock(&policy_lock);
    if (current_policy != new_policy) {
        current_policy = new_policy;
        printk(KERN_INFO "Partitioning policy changed to: %s\n", policy_names[current_policy]);
    } else {
        printk(KERN_INFO "Partitioning policy already set to: %s\n", policy_names[current_policy]);
    }
    spin_unlock(&policy_lock);

    return count;
}


/** Initializes the kobj_attribute struct with the reserves_show and partition_show & _store function
 *   - reserves, partition_policy is the name of the sysfs file
 *   - 0444 is the permissions of the sysfs file, read only for all users 0664 is read/write for owner and read only for group and others
 *   - reserves_show is the function to call when the user reads the sysfs file
 *   - partition_policy_show is the function to call when the user reads the sysfs file
 *   - partition_policy_store is the function to call when the user writes to the sysfs file
 */
static struct kobj_attribute reserves_attr = __ATTR(reserves, 0444, reserves_show, NULL);
static struct kobj_attribute partition_policy_attr = __ATTR(partition_policy, 0664, partition_policy_show, partition_policy_store);


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

static int create_partition_policy_file(void)
{
    int ret;

    if (!rtes_kobj) {
        printk(KERN_ERR "partition_policy: rtes_kobj is not initialized yet!\n");
        return -EINVAL;
    }

    // Create a sysfs file named "partition_policy" under the "rtes" kobject
    ret = sysfs_create_file(rtes_kobj, &partition_policy_attr.attr);
    if (ret) {
        printk(KERN_ERR "partition_policy: /sys/rtes/partition_policy creation failed\n");
        return ret;
    }

    printk(KERN_INFO "partition_policy: Created file /sys/rtes/partition_policy\n");
    return 0;
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
    initialize_processors();
    ret = create_reserves_file();
    if (ret != 0) {
        printk(KERN_ERR "Failed to create reserves file\n");
        return ret;
    }

    // Initialize the spinlock for the reserved tasks list
    spin_lock_init(&reserved_tasks_list_lock);
    spin_lock_init(&processors_lock);

    return 0; // Success
}

static int __init partition_policy_init(void)
{
    int ret;

    // Initialize spinlock
    spin_lock_init(&policy_lock);

    // Create the sysfs file
    ret = create_partition_policy_file();
    if (ret) {
        printk(KERN_ERR "Failed to initialize partition_policy sysfs\n");
        return ret;
    }

    return 0;
}

// Use postcore so that it runs after rtes_kobj is initialized by taskmon
postcore_initcall(init_reserve);
postcore_initcall(partition_policy_init);


