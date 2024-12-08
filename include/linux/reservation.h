#ifndef _LINUX_RESERVATION_H
#define _LINUX_RESERVATION_H

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/kobject.h>
#include <linux/spinlock.h>

#define MAX_PROCESSORS 4
#define MAX_TASKS 16

enum partition_policy {
    FF,     // First Fit
    NF,     // Next Fit
    BF,     // Best Fit
    WF,      // Worst Fit
    LST     // List Scheduling
};

// Task structure for bin-packing
struct bucket_task_ll {
    struct task_struct *task;
    uint32_t util;
    struct timespec cost;
    struct timespec period;
    struct bucket_task_ll *next;
};

// Bucket (processor) information
struct bucket_info {
    uint32_t running_util;             // Total utilization on this processor
    int num_tasks;                     // Number of tasks assigned
    struct bucket_task_ll *first_task; // Linked list of tasks in this bucket
};


struct data_point {
    u64 timestamp;                  // Timestamp  which is actually period count 
	char utilization[32];              // Utilization as a string
    struct list_head list;         // Linked list node
};

// Task struct
struct reservation_data {
    /* Reservation Framework parameters*/
	struct timespec reserve_C;
	struct timespec reserve_T;
	struct hrtimer reservation_timer;
    struct hrtimer cost_timer;
    struct hrtimer period_timer;
	bool has_reservation;

	/* Computation time tracking */
	struct timespec exec_start_time;			// timestamp of task start
	u64 exec_accumulated_time;		// accumulated execution time

	/* TaskMon parameters */
	bool monitoring_enabled;
	struct kobject *taskmon_kobj;				// kobject for taskmon represented as /sys
	struct kobj_attribute *taskmon_tid_attr;
	spinlock_t data_lock;						
	struct list_head data_points;
	u64 period_count;

    struct task_struct *task;
    u64 energy_accumulator;                     // Energy accumulator (mJ)
    struct kobject *energy_kobj;                // kobject for energy monitoring

};

// Function declarations
extern bool taskmon_enabled;
int create_tid_file(struct task_struct *task);
void cleanup_utilization_data(struct task_struct *task);
void enable_monitoring_for_all_tasks(void);
void disable_monitoring_for_all_tasks(void);
int remove_tid_file(struct task_struct *task);


// Function declarations for bin packing
extern enum partition_policy current_policy;
extern struct bucket_info processors[MAX_PROCESSORS];
int find_best_processor(uint32_t util, enum partition_policy policy, struct timespec C, struct timespec T);
void add_task_to_processor(struct task_struct *task, struct timespec C, struct timespec T, int cpuid);
void remove_task_from_processor(struct task_struct *task);
void remove_task_from_list(struct task_struct *task);
void initialize_processors(void);

#endif // _LINUX_RESERVATION_H