#ifndef _LINUX_RESERVATION_H
#define _LINUX_RESERVATION_H

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/kobject.h>
#include <linux/spinlock.h>

struct data_point {
    u64 timestamp;                  // Timestamp  which is actually period count 
    u64 utilization;               // Utilization as per mille value
    struct list_head list;         // Linked list node
};

// Task struct
struct reservation_data {
    /* Reservation Framework parameters*/
	struct timespec reserve_C;
	struct timespec reserve_T;
	struct hrtimer reservation_timer;
	bool has_reservation;

	/* Computation time tracking */
	struct timespec exec_start_time;			// timestamp of task start
	struct timespec exec_accumulated_time;		// accumulated execution time

	/* TaskMon parameters */
	bool monitoring_enabled;
	struct kobject *taskmon_kobj;				// kobject for taskmon represented as /sys
	spinlock_t data_lock;						
	struct list_head data_points;
	u64 period_count;

    struct task_struct *task;

};

// Function declarations
extern bool taskmon_enabled;
int create_tid_file(struct task_struct *task);
void cleanup_utilization_data(struct task_struct *task);
void enable_monitoring_for_all_tasks(void);
void disable_monitoring_for_all_tasks(void);

#endif // _LINUX_RESERVATION_H