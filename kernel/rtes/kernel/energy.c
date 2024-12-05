#include "taskmon.h"
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>


bool energy_mon_enabled = false;
struct kobject *config_kobj; // kobject for /rtes/config
struct kobject *tasks_kobj;  // kobject for /rtes/tasks
static unsigned long system_frequency = 0;

// static unsigned long power_table_mw[12] = {
//     29, 36, 58, 100, 156, 240, 312, 378, 478, 556, 639, 727 // Pre-determined power?
// };


void energy_init(void){
    current_freq = cpufreq_quick_get(0); // kHz
    system_freq = current_freq / 1000; //mHz
    struct cpufreq_frequency_table *freq_table, *entry;
    struct cpufreq_policy *policy;
    int index = 0;

    policy =  cpufreq_cpu_get(0);
    if(!policy){
        printk(KERN_INFO "Failed to get CPU frequency policy.\n");
        return;
    }

    freq_table = cpufreq_frequency_get_table(policy->cpu);
    if(!freq_table){
        printk(KERNO_INFO "No CPU frequency table found.\n");
        cpufreq_cpu_put((policy))
        return;
    }

    for(entry = freq_table; entry->frequency != CPUFREQ_TABLE_END; entry++){
        if(entry->frequency == CPUFREQ_ENTRY_INVALID){
            continue;
        }

        if(current_freq == entry->frequency){
            system_power = power_table[index];
            break;
        }
    }
}

int __init init_energy_kobjects(void)
{
    // Create a kobject named "config" under the "rtes" kobject /sys/rtes/config
    config_kobj = kobject_create_and_add("config", rtes_kobj);
    if (!config_kobj)
    {
        printk(KERN_ERR "Failed to create kobject: config\n");
        kobject_put(rtes_kobj); // Release the kobject
        return -ENOMEM;
    }
    printk(KERN_INFO "Created kobject: /sys/rtes/config\n");

    // Create a kobject named "tasks" under the "rtes" kobject /sys/rtes/tasks
    tasks_kobj = kobject_create_and_add("tasks", rtes_kobj);
    if (!tasks_kobj)
    {
        printk(KERN_ERR "Failed to create kobject: tasks\n");
        kobject_put(rtes_kobj);
        return -ENOMEM;
    }
    printk(KERN_INFO "Created kobject: /sys/rtes/tasks\n");
    return 0;
}

// When the user reads the sysfs file
static ssize_t config_energy_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    // Return the current value of enabled as 0 or 1
    return sprintf(buf, "%d\n", (int)energy_mon_enabled);
}

// When the user writes to the sysfs file
static ssize_t config_energy_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    // Set energy_mon_enabled to true or false and start or stop monitoring accordingly
    if (buf[0] == '1')
    {
        energy_mon_enabled = true;
        // TODO: code here to start monitoring
        // Start monitoring
        printk(KERN_INFO "energymon enabled\n");
    }
    else if (buf[0] == '0')
    {
        energy_mon_enabled = false;
        // TODO: code here to stop monitoring
        // Stop monitoring
        printk(KERN_INFO "energymon disabled\n");
    }
    return count;
}
static ssize_t energy_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    // /sys/rtes/energy mJ total energy consumed by the system (all tasks present and past)
    // TODO: return the energy
    return sprintf(buf, "%d\n", (int)1234);
}
static ssize_t energy_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    // Writing any value to /sys/rtes/energy should reset the total energy accumulator to zero.
    // TODO: code here to reset the energy accumulator
    return 0;
}
static ssize_t freq_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct file *file;
    char freq_buf[16];
    mm_segment_t old_fs;
    ssize_t count = 0;

    // Change the file context to kernel space
    old_fs = get_fs();
    set_fs(KERNEL_DS);

    // Open the file
    file = filp_open("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq", O_RDONLY, 0);
    if (IS_ERR(file))
    {
        pr_err("Unable to open cpuinfo_cur_freq file\n");
        set_fs(old_fs);
        return -ENOENT;
    }

    // Read the frequency value from the file
    count = kernel_read(file, freq_buf, sizeof(freq_buf) - 1, &file->f_pos);
    if (count > 0)
    {
        freq_buf[count] = '\0';                // Null-terminate the string
        sscanf(freq_buf, "%d", (int *)&count); // Convert to integer if needed
    }

    // Close the file
    filp_close(file, NULL);
    set_fs(old_fs);

    // Return the read frequency value
    return sprintf(buf, "%d\n", count);
}
static ssize_t power_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    // /sys/rtes/power mW total power consumption of the system
    // TODO: return the power
    return sprintf(buf, "%lu\n", system_power_mw);
}

// File attributes
// Read/Write files
static struct kobj_attribute config_attr = __ATTR(energy, 0660, config_energy_show, config_energy_store);
static struct kobj_attribute energy_attr = __ATTR(energy, 0660, energy_show, energy_store);
// Readonly files
static struct kobj_attribute freq_attr = __ATTR(freq, 0440, freq_show, NULL);
static struct kobj_attribute power_attr = __ATTR(power, 0444, power_show, NULL);

int create_energy_files(void)
{
    int ret;
    // Create /sys/rtes/config/energy
    ret = sysfs_create_file(config_kobj, &config_attr.attr);
    if (ret)
    {
        printk(KERN_ERR "Failed to create file: /sys/rtes/config/energy\n");
        return -1;
    }
    printk(KERN_INFO "Created file: /sys/rtes/config/energy\n");

    // Create /sys/rtes/energy
    ret = sysfs_create_file(rtes_kobj, &energy_attr.attr);
    if (ret)
    {
        printk(KERN_ERR "Failed to create file: /sys/rtes/energy\n");
        return -1;
    }

    // Create /sys/rtes/freq
    ret = sysfs_create_file(rtes_kobj, &freq_attr.attr);
    if (ret)
    {
        printk(KERN_ERR "Failed to create file: /sys/rtes/freq\n");
        return -1;
    }

    // Create /sys/rtes/power
    ret = sysfs_create_file(rtes_kobj, &power_attr.attr);
    if (ret)
    {
        printk(KERN_ERR "Failed to create file: /sys/rtes/power\n");
        return -1;
    }

    // Dynamically create /sys/rtes/tasks/<pid>/energy
    // TODO dynamically create the pid files

    return 0; // Success
}

/**
 * Create all the /sys files for energy
 * /sys/rtes/config/energy      :   enable/disable energy monitoring
 * /sys/rtes/freq               :   MHz processor frequency
 * /sys/rtes/power              :   mW total power consumption
 * /sys/rtes/tasks/<pid>/energy   :   mJ energy consumed by each task
 * /sys/rtes/energy             :   mJ total energy consumed by the system
 */
int __init init_energy(void)
{
    int ret;

    // Init energy kobjects
    ret = init_energy_kobjects();
    if (ret != 0)
    {
        printk(KERN_ERR "Failed to initialize energy kobjects\n");
        return ret;
    }

    ret = create_energy_files();
    if (ret != 0)
    {
        printk(KERN_ERR "Failed to create energy files\n");
        return ret;
    }

    return 0; // Success
}

// Use postcore so that it runs after rtes_kobj is initialized by taskmon
postcore_initcall(init_energy);