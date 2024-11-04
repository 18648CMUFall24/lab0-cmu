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
    struct timespec budget = res_data->reserve_C;
    struct timespec exec_time = res_data->exec_accumulated_time;
    struct data_point *point;
    struct timespec zero = {0, 0};
    unsigned long flags;
    u64 budget_ns, exec_ns, period_ns, utilization;

    budget_ns = timespec_to_ns(&res_data->reserve_C);
    exec_ns = timespec_to_ns(&res_data->exec_accumulated_time);
    period_ns = timespec_to_ns(&res_data->reserve_T);

    printk(KERN_INFO "reservation_timer_callback: PID %d accumulated execution time: %llu ns\n",
           task->pid, exec_ns);

    if (timespec_compare(&exec_time, &budget) > 0) {
        // Send SIGEXCESS signal to process
        struct siginfo info; 
        memset(&info, 0, sizeof(struct siginfo));
        info.si_signo = SIGEXCESS;
        info.si_code = SI_KERNEL;

        if (send_sig_info(SIGEXCESS, &info, task) < 0) {
            printk(KERN_ERR "Failed to send SIGEXCESS to PID %d\n", task->pid);
        } else {
            printk(KERN_INFO "SIGEXCESS sent to PID %d\n", task->pid);
        }
    }

    // Calculate utilization as a per mille value (e.g., 500 means 50%)
    utilization = div64_u64(exec_ns * 1000, period_ns);
    res_data->period_count++;  // Increment the period count

    // Collect utilization data if monitoring is enabled
    if (res_data->monitoring_enabled) {
        point = kmalloc(sizeof(*point), GFP_ATOMIC);
        if (point) {
            point->timestamp = div64_u64((u64)res_data->period_count * timespec_to_ns(&res_data->reserve_T), 1000000); // Convert to ms
            point->utilization = utilization; 
            spin_lock_irqsave(&res_data->data_lock, flags);
            list_add_tail(&point->list, &res_data->data_points);
            spin_unlock_irqrestore(&res_data->data_lock, flags);
        } else {
            printk(KERN_ERR "Failed to allocate memory for data point\n");
        }
    }

    res_data->exec_accumulated_time = zero;  // Reset accumulated time

    hrtimer_forward_now(timer, timespec_to_ktime(res_data->reserve_T));  // Forward timer to next period
    return HRTIMER_RESTART;
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

    // Create per-thread sysfs file if monitoring is enabled
    if (res_data->monitoring_enabled) {
        create_tid_file(task);
    }

    res_data->exec_accumulated_time = (struct timespec){0, 0};
    res_data->exec_start_time = (struct timespec){0, 0};

  
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
    hrtimer_init(&res_data->reservation_timer, CLOCK_MONOTONIC, HRTIMER_MODE_PINNED);
    res_data->reservation_timer.function = reservation_timer_callback;
    res_data->task = task;  // Link task to reservation data for callback
    hrtimer_start(&res_data->reservation_timer, timespec_to_ktime(t), HRTIMER_MODE_PINNED);

    if (pid != 0) 
        put_task_struct(task);

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
    res_data->has_reservation = false;
    set_cpus_allowed_ptr(task, cpu_all_mask);

   
    cleanup_utilization_data(task);

    if (pid != 0) 
        put_task_struct(task);

    printk(KERN_INFO "cancel_reserve: Reservation cancelled for PID %d\n", task->pid);
    return 0;
}