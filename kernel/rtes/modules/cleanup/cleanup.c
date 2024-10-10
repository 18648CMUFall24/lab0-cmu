/**
 * 4.5.3 Override syscalls at runtime (5 points)
 * Source code location: kernel/rtes/modules/cleanup/cleanup.c
 * 
 * TODO:
 * 1- Intercept when a process exits: when a process exits, report paths of files that were left open.
 * 2- Use syscall override: override a relevant syscall and do custom processing.
 * 3- Filter by process name: only report files left open by processes with a specific name. (comm = "sloppyapp")
 * 4- The override should only be active when the module is loaded, the original syscall 
 * should be restored when the module is unloaded.
 * 
 * Steps:
 * $ insmod cleanup comm="sloppyapp"
 * 
 * A report should be visible in dmesg after a process exits without having closed one or more files. There
 * should be no output at all if no files were left open. The output should be formatted as follows:
 * 
 * cleanup: process 'sloppyapp' (PID 123) did not close files:
 * cleanup: /path/to/file1
 * cleanup: /path/to/file2
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fdtable.h>
#include <linux/uaccess.h>
#include <linux/kallsyms.h>
#include <linux/unistd.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mm_types.h>
#include <linux/slab.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>


MODULE_LICENSE("Dual BSD/GPL");

// stored the name of process that the module should monitor, and the default is "sloppyapp"
static char *comm = "sloppyapp";
module_param(comm, charp, 0644);

//access the address of the syscall table
void **syscall_table;
// pointer to store the original exit syscall
asmlinkage long (*original_sys_exit_group)(int code);

// Define L_PTE_RDONLY if not defined
#ifndef L_PTE_RDONLY
#define L_PTE_RDONLY    (1 << 7)  // Verify this value for your kernel
#endif

// void make_rw(unsigned long address)
// {
//     pte_t *pte = lookup_address(address, NULL);

//     if (pte && (pte_val(*pte) & L_PTE_RDONLY)) {
//         set_pte(pte, __pte(pte_val(*pte) & ~L_PTE_RDONLY));
//         flush_tlb_all();
//     }
// }

// void make_ro(unsigned long address)
// {
//     pte_t *pte = lookup_address(address, NULL);

//     if (pte && !(pte_val(*pte) & L_PTE_RDONLY)) {
//         set_pte(pte, __pte(pte_val(*pte) | L_PTE_RDONLY));
//         flush_tlb_all();
//     }
// }

// The hooked do_exit function
asmlinkage long my_exit_group(int code) {
    struct task_struct *task = current;             // get the current task
    struct files_struct *files;                     // get the files_struct of the task
    struct fdtable *fdt;                            // file descriptor table
    int fd;  
    struct file *file;                              // file pointer
    int reported = 0;                               // flag to indicate if any files were left open
    char *tmp;
    char *path;
    // printk(KERN_INFO "cleanup: hooked_exit called for process: %s (PID: %d)\n", task->comm, task->pid);

    if (strcmp(task->comm, comm) == 0) {          // check if the process name matches the filter
        printk(KERN_INFO "cleanup: process name matched: %s\n", task->comm);
        files = task->files;                        // get the files_struct of the task

        if (files) {
            spin_lock(&files->file_lock);            // lock the file lock
            fdt = files_fdtable(files);              // get the file descriptor table
            for (fd = 0; fd < fdt->max_fds; fd++) {
                file = fdt->fd[fd];                  // get the file pointer
                if (file) {
                    if (!reported) {
                        printk(KERN_INFO "cleanup: process '%s' (PID %d) did not close files:\n", task->comm, task->pid);
                        reported = 1;
                    }

                    tmp = kmalloc(PATH_MAX, GFP_KERNEL);
                    if (tmp) {
                        path = d_path(&file->f_path, tmp, PATH_MAX);  // get the path of the file
                        if (!IS_ERR(path)) {
                            printk(KERN_INFO "cleanup: %s\n", path);
                        } else {
                            printk(KERN_INFO "cleanup: error getting file path\n");
                        }
                        kfree(tmp);
                    } else {
                        printk(KERN_INFO "cleanup: error allocating memory\n");
                    }
                }
            }
            spin_unlock(&files->file_lock);         // unlock the file lock
        }
    }
    return original_sys_exit_group(code); // Call the original function to complete exit
}

// Module initialization
static int __init cleanup_init(void) {
    // Override do_exit
    // original_exit = sys_call_table[__NR_exit];
    // disable_write_protection(&sys_call_table[__NR_exit]);
    syscall_table = (void **)kallsyms_lookup_name("sys_call_table");
    if (!syscall_table) {
        printk(KERN_INFO "cleanup: sys_call_table not found\n");
        return -1;
    }

    original_sys_exit_group = syscall_table[__NR_exit_group];
    // make_rw((unsigned long)syscall_table);
    syscall_table[__NR_exit_group] = my_exit_group;
    // make_ro((unsigned long)syscall_table);

    printk(KERN_INFO "cleanup: module loaded\n");
    return 0;
}

// Module cleanup
static void __exit cleanup_exit(void) {

    // make_rw((unsigned long)syscall_table);
    syscall_table[__NR_exit_group] = original_sys_exit_group;
    // make_ro((unsigned long)syscall_table);
    printk(KERN_INFO "cleanup: module unloaded\n");
}

module_init(cleanup_init);
module_exit(cleanup_exit);

