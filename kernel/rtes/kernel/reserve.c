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


enum hrtimer_restart reservation_timer_callback(struct hrtimer *timer) {
    struct task_struct *task = container_of(timer, struct task_struct, reservation_timer); // get the address of the task strcut that contain this timer
    struct timespec budget = task->reserve_C;
    struct timespec period = task->reserve_T;
    struct timespec exec_time = task->exec_accumulated_time;
    struct data_point *point;
    struct timespec zero = {0, 0};
    u64 budget_ns = timespec_to_ns(&task->reserve_C);;
    u64 exec_ns = timespec_to_ns(&task->exec_accumulated_time);
    printk(KERN_INFO "reservation_timer_callback: PID %d accumulated execution time: %llu ns\n",
       task->pid, exec_ns);

    if (timespec_compare(&exec_time, &budget) > 0) {
        // send SIGEXCESS signal to process
        struct siginfo info; 
        memset(&info, 0, sizeof(struct siginfo));
        info.si_signo = SIGEXCESS;
        info.si_code = SI_KERNEL;
        printk(KERN_WARNING "Budget overrun detected for PID %d: execution time %llu ns exceeded budget %llu ns\n",
               task->pid, task->exec_accumulated_time, budget_ns);

        /* Send the signal to the task's process */
        if (send_sig_info(SIGEXCESS, &info, task) < 0) {
            printk(KERN_ERR "Failed to send SIGEXCESS to PID %d\n", task->pid);
        } else {
            printk(KERN_INFO "SIGEXCESS sent to PID %d\n", task->pid);
        }
    }

    // utilization = (exec_time / period) * scaling_factor
    u64 exec_ns = timespec_to_ns(&exec_time);
    u64 period_ns = timespec_to_ns(&period);
    u32 utilization = div64_u64(exec_ns * 1000, period_ns);
    task->exec_accumulated_time = zero;     // timespec is zeroed

    // collect utilization data if monitoring is enabled
    if (task->monitoring_enabled) {
        point = kmalloc(sizeof(*point), GFP_ATOMIC);
        if (point) {
            getrawmonotonic(&point->timestamp);
            point->utilization = exec_time;

            mutex_lock(&task->data_lock);
            list_add_tail(&point->list, &task->data_points);
            mutex_unlock(&task->data_lock);
        }
    }

    hrtimer_forward_now(timer, timespec_to_ktime(task->reserve_T)); // forward timer to next period
    return HRTIMER_RESTART; // trigger periodically
}

SYSCALL_DEFINE4(set_reserve, pid_t, pid, struct timespec __user *, C, struct timespec __user *, T, int, cpuid) {
    struct task_struct *task;
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

    if (task->has_reservation) {
        hrtimer_cancel(&task->reservation_timer);
    }

    // inti monitoring data
    task->monitoring_enabled = taskmon_enabled;
    mutex_init(&task->data_mutex);
    INIT_LIST_HEAD(&task->data_points);

    // Create per-thread sysfs file if monitoring is enabled
    if (task->monitoring_enabled)
        create_tid_file(task);

    task->exec_accumulated_time = (struct timespec){0, 0};
    task->exec_start_time = (struct timespec){0, 0};

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

    spin_lock(&task->alloc_lock);
    // store the paremeters in the task struct
    task->reserve_C = c;
    task->reserve_T = t;
    task->has_reservation = true;
    
    // initialize a high resolution timer trigger periodically T units
    hrtimer_init(&task->reservation_timer, CLOCK_MONOTONIC, HRTIMER_MODE_PINNED);
    task->reservation_timer.function = reservation_timer_callback;
    hrtimer_start(&task->reservation_timer, timespec_to_ktime(t), HRTIMER_MODE_PINNED);
    spin_unlock(&task->alloc_lock);

    if (pid != 0) 
        put_task_struct(task);

    printk(KERN_INFO "set_reserve called: pid=%d, C=%ld.%09ld, T=%ld.%09ld, cpuid=%d\n",
           pid, c.tv_sec, c.tv_nsec, t.tv_sec, t.tv_nsec, cpuid);

    return 0;
}


//remove the thread
SYSCALL_DEFINE1(cancel_reserve, pid_t, pid) {
    struct task_struct *task;
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
    if (!task->has_reservation) {
        if(pid != 0) {
            put_task_struct(task);
        }
        return -EINVAL;
    }

    // cancel hrtimer
    hrtimer_cancel(&task->reservation_timer);

    // clear up reservation parameters
    task->reserve_C = (struct timespec){0, 0};
    task->reserve_T = (struct timespec){0, 0};
    task->has_reservation = false;

    set_cpus_allowed_ptr(task, cpu_all_mask);

    if (task->rtes_kobj) {
        sysfs_remove_file(task->rtes_kobj, &task->tid_attr.attr);
        kobject_put(task->rtes_kobj);
        task->rtes_kobj = NULL;
    }

    cleanup_utilization_data(task);

    if (pid != 0) 
        put_task_struct(task);

    printk(KERN_INFO "cancel_reserve: Reservation cancelled for PID %d\n", task->pid);
    return 0;
}