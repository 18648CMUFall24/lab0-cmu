#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/sched.h>


struct reservation_info {
    struct timespec C;
    struct timespec T;
    struct hrtimer timer;
    u64 time;
    pid_t tid; // thread id
    struct list_head list; // linked list node
};

static LIST_HEAD(reservation_list);
static DEFINE_MUTEX(reservation_lock);


static enum hrtimer_restart timer_callback(struct hrtimer* timer){
    struct reservation_info *resv = container_of(timer, struct reservation_info, timer);

    resv->time = 0;
    hrtimer_forward_now(timer, timespec_to_ktime(resv->T));
    printk(KERN_INFO "Timer callback: Reset accumulator for TID %d\n", resv->tid);
    return HRTIMER_RESTART;
}

int set_reservation(pid_t tid, struct timespec *C, struct timespec *T, int cpuid){
    struct reservation_info *resv;
    struct task_struct *task;
    cpumask_t mask;

    task = get_pid_task(find_vpid(tid), PIDTYPE_PID);
    if(!task){
        return -ESRCH;
    }
    cpumask_clear(&mask);
    cpumask_set_cpu(cpuid, &mask);
    set_cpus_allowed_ptr(task, &mask);

    mutex_lock(&reservation_lock);

    list_for_each_entry(resv, &reservation_list, list){
        if(resv->tid == tid){
            printk(KERN_INFO "update reservation for TID: %d\n", tid);
            resv->C = *C;
            resv->T = *T;
            hrtimer_start(&resv->timer, timespec_to_ktime(*T), HRTIMER_MODE_REL_PINNED);
            mutex_unlock(&reservation_lock);
            return 0;
        }
    }

    resv = kmalloc(sizeof(struct reservation_info), GFP_KERNEL);
    if(!resv) {
        mutex_unlock(&reservation_lock);
        return -ENOMEM;
    }

    resv->tid = tid;
    resv->C = *C;
    resv->T = *T;
    resv->time = 0;

    hrtimer_init(&resv->timer, CLOCK_MONOTONIC, HRTIMER_MODE_PINNED);

    list_add(&resv->list, &reservation_list);
    mutex_unlock(&reservation_lock);
    printk(KERN_INFO "Reservation set for TID: %d\n", tid);
    return 0;
}

int cancel_reservation(pid_t tid) {
    struct reservation_info *resv, *temp;
    mutex_lock(&reservation_lock);
    list_for_each_entry_safe(resv, temp, &reservation_list, list){
        if(resv->tid == tid){
            hrtimer_cancel(&resv->timer);
            list_del(&resv->list);
            kfree(resv);
            mutex_unlock(&reservation_lock);
            printk(KERN_INFO "Reservation cancelled for TID: %d\n", tid);
            return 0;
        }
    }
    mutex_unlock(&reservation_lock);
    return -EINVAL;
}

static int __init accumulator_init(void) {
    printk(KERN_INFO "Accumulator module loaded.\n");
    return 0;
}

static void __exit accumulator_exit(void) {
    struct reservation_info *resv, *temp;
    list_for_each_entry_safe(resv, temp, &reservation_list, list){
        hrtimer_cancel(&resv->timer);
        list_del(&resv->list);
        kfree(resv);
    }
    mutex_unlock(&reservation_lock);

    printk(KERN_INFO "Accumulator module unloaded.\n");
}


module_init(accumulator_init);
module_exit(accumulator_exit);