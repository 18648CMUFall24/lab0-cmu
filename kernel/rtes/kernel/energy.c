#include "taskmon.h"
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/string.h>


bool energy_mon_enabled = false;
struct kobject *config_kobj; // kobject for /rtes/config
struct kobject *tasks_kobj;  // kobject for /rtes/tasks

typedef struct {
    uint32_t freq; 
    uint32_t power;
} FreqPower;
#define NUM_FREQS 12
// scaling_available_frequencies (in kHz)                                                                 
// 51000 102000 204000 340000 475000 640000 760000 860000 1000000 1100000 1200000 1300000 
FreqPower freq_to_power_table[] = {
    // {freq, power*10^3}
    // Power is multiplied by 3
    {51, 28860}, // f=51 MHz
    {102, 35715}, // f=102 MHz
    {204, 57525}, // f=204 MHz
    {340, 100363}, // f=340 MHz
    {475, 156186}, // f=475 MHz
    {640, 240375}, // f=640 MHz
    {760, 311729}, // f=760 MHz
    {860, 377308}, // f=860 MHz
    {1000, 478015}, // f=1000 MHz
    {1100, 556052}, // f=1100 MHz
    {1200, 638994}, // f=1200 MHz
    {1300, 726703}, // f=1300 MHz
};


void energy_init(void){
    // uint32_t current_freq = cpufreq_quick_get(0); // kHz
    // uint32_t system_freq = current_freq / 1000; //mHz
    // struct cpufreq_frequency_table *freq_table, *entry;
    // struct cpufreq_policy *policy;
    // int index = 0;

    // policy =  cpufreq_cpu_get(0);
    // if(!policy){
    //     printk(KERN_INFO "Failed to get CPU frequency policy.\n");
    //     return;
    // }

    // freq_table = cpufreq_frequency_get_table(policy->cpu);
    // if(!freq_table){
    //     printk(KERN_INFO "No CPU frequency table found.\n");
    //     cpufreq_cpu_put((policy));
    //     return;
    // }

    // for(entry = freq_table; entry->frequency != CPUFREQ_TABLE_END; entry++){
    //     if(entry->frequency == CPUFREQ_ENTRY_INVALID){
    //         continue;
    //     }

    //     if(current_freq == entry->frequency){
    //         system_power = power_table[index];
    //         break;
    //     }
    // }
}

int __init init_energy_kobjects(void)
{
    if (rtes_kobj == NULL){
        printk(KERN_ERR "energy: critical error - rtes_obj is not initialized yet!\n");
        return -1;
    }
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
    return count;
}
static ssize_t freq_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct file *file;
    char freq_buf[16];
    mm_segment_t old_fs;
    ssize_t len;
    int freq_khz = 0; // To store the frequency in kHz

    // Change the file context to kernel space
    old_fs = get_fs();
    set_fs(KERNEL_DS);

    // Open the file
    file = filp_open("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq", O_RDONLY, 0);
    if (IS_ERR(file)) {
        pr_err("Unable to open cpuinfo_cur_freq file\n");
        set_fs(old_fs);
        return -ENOENT;
    }

    // Read the frequency value from the file
    len = kernel_read(file, file->f_pos, freq_buf, sizeof(freq_buf) - 1);
    if (len < 0) {
        pr_err("Failed to read from cpuinfo_cur_freq\n");
        filp_close(file, NULL);
        set_fs(old_fs);
        return len;  // Return error code
    }

    // Null-terminate the buffer
    freq_buf[len] = '\0';

    // Convert the string to an integer (in kHz)
    if (kstrtoint(freq_buf, 10, &freq_khz)) {
        pr_err("Failed to convert frequency to integer\n");
        filp_close(file, NULL);
        set_fs(old_fs);
        return -EINVAL;  // Return error code
    }

    // Close the file
    filp_close(file, NULL);
    set_fs(old_fs);

    // Convert from kHz to MHz
    freq_khz = freq_khz / 1000;

    // Return the frequency value in MHz
    return sprintf(buf, "%d\n", freq_khz);
}
// Read and return int value from /sys/rtes/freq file
int read_freq(void) {
    struct file *file;
    char buffer[64];
    loff_t pos = 0;
    ssize_t len;
    int freq = 0;

    // Open the file for reading
    file = filp_open("/sys/rtes/freq", O_RDONLY, 0);
    if (IS_ERR(file)) {
        pr_err("Failed to open /sys/rtes/freq\n");
        return PTR_ERR(file);  // Return error code
    }

    // Read the contents of the file into the buffer
    len = kernel_read(file, file->f_pos, buffer, sizeof(buffer) - 1);

    if (len < 0) {
        pr_err("Failed to read from /sys/rtes/freq\n");
        filp_close(file, NULL);
        return len;  // Return error code
    }

    // Null-terminate the buffer
    buffer[len] = '\0';

    // Convert the string to an integer
    if (kstrtoint(buffer, 10, &freq)) {
        pr_err("Failed to convert frequency to integer\n");
        filp_close(file, NULL);
        return -EINVAL;  // Return error code
    }

    // Close the file
    filp_close(file, NULL);

    // Return the integer value
    return freq;
}

static ssize_t power_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    // /sys/rtes/power mW total power consumption of the system
    /** P(f) = k*f^{alpha} + beta
     *      k = 0.00442 mW/MHz
     *      alpha = 1.67
     *      beta = 25.72 mW */ 
    uint32_t i=0, freq=0;
    freq = read_freq(); // MHz
    
    for (i=0; i<NUM_FREQS; i++) {
        // Iterate through table to get power for this frequency
        if (freq_to_power_table[i].freq == freq) {
            return sprintf(buf, "%lu\n", freq_to_power_table[i].power); 
        }
    }
    // Did not find in table
    return sprintf(buf, "%lu\n", 0);
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
static int __init init_energy(void)
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