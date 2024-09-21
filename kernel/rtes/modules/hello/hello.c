/**
 * 4.3 Loadable Kernel Module (10 points)
 * Source code location: kernel/rtes/modules/hello/hello.c
 * Write a loadable kernel module which upon loading on the target
 * device prints the following to the kernel log:
 *
 * Hello, world! Kernel-space -- the land of the free and the home of the brave.
 */

#include <linux/init.h>
#include <linux/module.h>
MODULE_LICENSE("Dual BSD/GPL");
static int hello_init(void)
{
    printk(KERN_ALERT "Hello, world! Kernel-space -- the land of the free and the home of the brave.\n");
    return 0;
}

static void hello_exit(void)
{
}
module_init(hello_init);
module_exit(hello_exit);