#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fdtable.h>
#include <linux/uaccess.h>
#include <linux/kallsyms.h>
#include <linux/unistd.h>  // For syscall numbers like __NR_exit

static char *comm_filter = "sloppyapp";
module_param(comm_filter, charp, 0000);

unsigned long *sys_call_table;
asmlinkage void (*original_do_exit)(long code);

// Function to find sys_call_table dynamically
unsigned long *get_sys_call_table(void) {
    return (unsigned long *)kallsyms_lookup_name("sys_call_table");
}

#ifndef __NR_exit
    #define __NR_exit 1  // ARM typically uses 1 for exit, adjust if needed
#endif

// The hooked do_exit function
asmlinkage void hooked_do_exit(long code) {
    struct task_struct *task = current;
    struct files_struct *files = task->files;
    struct fdtable *fdt;
    int fd;
    struct file *file;

    if (strstr(task->comm, comm_filter)) {
        fdt = files_fdtable(files);
        for (fd = 0; fd < fdt->max_fds; fd++) {
            file = fdt->fd[fd];
            if (file) {
                printk(KERN_INFO "cleanup: process '%s' (PID %d) did not close file: %s\n",
                       task->comm, task->pid, file->f_path.dentry->d_name.name);
            }
        }
    }

    original_do_exit(code); // Call the original function to complete exit
}

// Module initialization
static int __init cleanup_init(void) {
    sys_call_table = get_sys_call_table();
    if (!sys_call_table) {
        printk(KERN_ERR "cleanup: unable to locate sys_call_table\n");
        return -1;
    }

    // Override do_exit
    original_do_exit = (void *)sys_call_table[__NR_exit];
    sys_call_table[__NR_exit] = (unsigned long)hooked_do_exit;

    printk(KERN_INFO "cleanup: module loaded\n");
    return 0;
}

// Module cleanup
static void __exit cleanup_exit(void) {
    // Restore original do_exit
    sys_call_table[__NR_exit] = (unsigned long)original_do_exit;

    printk(KERN_INFO "cleanup: module unloaded\n");
}

module_init(cleanup_init);
module_exit(cleanup_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Intercepts process exits and logs unclosed files");
