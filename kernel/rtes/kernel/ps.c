/**
 * Process Information Syscalls (15 points)
 * Source code location: kernel/rtes/kernel/ps.c
 */


/**
 * Add a new system call count_rt_threads that can be used to obtain an 
 * integer value that equals the total number of real-time threads that 
 * currently exist in the system (see Section 2.4).
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/list.h>


SYSCALL_DEFINE0(count_rt_threads) {
    struct task_struct *task, *t;       // task handle process,  t handle thread
    int num_rt_threads = 0;
    rcu_read_lock(); // Start the read-side critical section

    // Loop thru all processes
    do_each_thread(task, t) {
        if (t->rt_priority > 0) {       // only count real-time threads
            num_rt_threads++; 
        }
    } while_each_thread(task, t);
    
    rcu_read_unlock(); // End the read-side critical section 
    return num_rt_threads;
}


/**
 * Add a new system call list_rt_threads that can be used to obtain a 
 * data structure with the list of real-time threads that currently 
 * exist in the system. The following attributes should be available for each
 * thread: TID, PID, real-time priority and name (command).
 */
struct rt_thread {
    pid_t tid;      /* Thread ID */
    pid_t pid;      /* Process ID */
    int priority;   /* Thread Priority */ 
    char name[20];     /* Name (command) */ 
};

SYSCALL_DEFINE2 (list_rt_threads, struct __user rt_thread *, rt_thread_list, unsigned int, num_threads) {
    struct task_struct *task, *t;       // task handle process,  t handle thread
    int i = 0;
    struct rt_thread rt_info;

    rcu_read_lock(); // Start the read-side critical section
    printk(KERN_INFO "ps: HERE 0\n");

    // Loop thru all processes
    for_each_process(task) {
        t = task;
        do {
            if (t->rt_priority > 0) {
                if (i > num_threads) {
                    printk(KERN_WARNING "ps: Thread list reached desired list size\n");
                    break; // Exit, reached desired list size
                }

                rt_info.tid = t->pid;
                rt_info.pid = task->tgid;
                rt_info.priority = t->rt_priority;
                strncpy(rt_info.name, t->comm, 20);
                rt_info.name[19] = '\0'; // Ensure null-terminated string

                // Print the data being copied for debugging
                printk(KERN_INFO "ps: Copying thread info to user space:\n");
                printk(KERN_INFO "ps: tid = %d, pid = %d, priority = %d, name = %s\n", 
                        rt_info.tid, rt_info.pid, rt_info.priority, rt_info.name);

                // Print destination user-space pointer info for debugging
                printk(KERN_INFO "ps: Destination rt_thread_list[%d] = %p, copying size = %u bytes\n",
                        i, &rt_thread_list[i], sizeof(struct rt_thread));


                // unsigned long copy_to_user (void __user * to, const void * from, unsigned long n);
                if (copy_to_user(rt_thread_list + i, &rt_info, sizeof(struct rt_thread))) {
                    printk(KERN_WARNING "ps: Error copying thread info to user space\n");
                    rcu_read_unlock();
                    return -EFAULT;
                }
                i++;
            }
            t = next_thread(t);
        } while(t != task);
    }
 
    rcu_read_unlock(); // End the read-side critical section 
    return i;
}