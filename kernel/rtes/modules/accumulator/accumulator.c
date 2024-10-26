#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

static int __init accumulator_init(void) {
    printk(KERN_INFO "Accumulator module loaded.\n");
    return 0;
}

static void __exit accumulator_exit(void) {
    printk(KERN_INFO "Accumulator module unloaded.\n");
}


module_init(accumulator_init);
module_exit(accumulator_exit);