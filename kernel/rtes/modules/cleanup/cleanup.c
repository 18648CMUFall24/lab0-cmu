#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/fdtable.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/moduleparam.h>


static char *comm = "sloppyapp";  // Default process name to track
module_param(comm, charp, 0644);  // Allow comm to be passed as a module parameter
MODULE_PARM_DESC(comm, "Substring of process name to track for open files");

asmlinkage long (*original_exit_group)(int);

asmlinkage long hooked_exit_group(int error_code) {
    struct task_struct *task = current;  // Get the current task (process)
    struct files_struct *files = task->files;
    struct fdtable *fdt;
    struct path files_path;
    char *buf;
    char *path;
    int fd;

    if (strstr(task->comm, comm)) {
        fdt = files_fdtable(files);

        for (fd = 0; fd < fdt->max_fds; fd++) {
            if (fdt->fd[fd]) {
                files_path = fdt->fd[fd]->f_path;

                buf = (char *)__get_free_page(GFP_KERNEL);
                if (!buf) {
                    printk(KERN_ERR "cleanup: Failed to allocate memory for file path\n");
                    continue;
                }

                path = d_path(&files_path, buf, PAGE_SIZE);
                if (!IS_ERR(path)) {
                    printk(KERN_INFO "cleanup: process '%s' (PID %d) did not close file: %s\n", task->comm, task->pid, path);
                }
                free_page((unsigned long)buf);
            }
        }
    }

    return original_exit_group(error_code);  // Call the original exit_group
}

// Hook the exit_group syscall
static int __init cleanup_init(void) {
    printk(KERN_INFO "cleanup: Module loaded to track process '%s'\n", comm);

    // Replace exit_group with our hooked function
    original_exit_group = (void *)kallsyms_lookup_name("sys_exit_group");
    if (original_exit_group == NULL) {
        printk(KERN_ERR "cleanup: Failed to find sys_exit_group symbol\n");
        return -1;
    }

    write_cr0(read_cr0() & (~0x10000));  // Disable write protection
    *((unsigned long *)kallsyms_lookup_name("sys_call_table") + __NR_exit_group) = (unsigned long)hooked_exit_group;
    write_cr0(read_cr0() | 0x10000);     // Re-enable write protection

    return 0;
}

// Unhook the syscall and restore original
static void __exit cleanup_exit(void) {
    write_cr0(read_cr0() & (~0x10000));  // Disable write protection
    *((unsigned long *)kallsyms_lookup_name("sys_call_table") + __NR_exit_group) = (unsigned long)original_exit_group;
    write_cr0(read_cr0() | 0x10000);     // Re-enable write protection

    printk(KERN_INFO "cleanup: Module unloaded\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("LKM to track open files of a process on exit");

module_init(cleanup_init);
module_exit(cleanup_exit);
