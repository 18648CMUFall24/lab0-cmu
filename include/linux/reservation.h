#ifndef _LINUX_RESERVATION_H
#define _LINUX_RESERVATION_H

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/time.h>

struct data_point {
    struct timespec timestamp;     // Timestamp when data point was collected
    struct timespec utilization;   // Utilization as time spent executing
    struct list_head list;         // Linked list node
};

// Function declarations
extern bool taskmon_enabled;
void init_taskmon(void);
int create_tid_file(struct task_struct *task);
void cleanup_utilization_data(struct task_struct *task);
void enable_monitoring_for_all_tasks(void);
void disable_monitoring_for_all_tasks(void);

#endif // _LINUX_RESERVATION_H