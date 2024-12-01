/**
 * Source code location: kernel/rtes/apps/easyperiodic/easyperiodic.c
 * Create a new native application easyperiodic with the same semantics as the periodic application
 * from Lab 2 but take pleasure in implementing it using your powerful end_job abstraction. Ensure that the
 * utilization collection, export via sysfs, and its display in TaskMon that you implemented in Lab 2 still
 * works for testing purposes.
 * periodic.c description:
 *    Write a native userspace application that takes C, T, cpuid arguments on the command line and busy-
 *    loops for C ms every T ms on the CPU cpuid. Support C and T values up to 60,000 ms (60 secs). The app
 *    should be runnable on the stock kernel too: it does not rely on any of your modifications to the kernel.
 *    Use this app to test your budget accounting and task monitor.
 *
 * For example, to create a periodic task that performs 250 ms of computation every 500 ms on CPU 0:
 *      $ ./easyperiodic 250 500 0
 *
 * Check online CPUs: `cat /sys/devices/system/cpu/online`
 */

#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <asm/unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>

#define SIGEXCESS 33
#define _NR_end_job 381

int parse_cmd_args(int argc, char *argv[], int32_t *C, int32_t *T, int32_t *cpuid)
{
    // Check if the number of arguments is correct
    if (argc != 4)
    {
        printf("Usage: %s <C> <T> <cpuid>\n", argv[0]);
        return -1; // Return error
    }

    // Parse the arguments to integers
    *C = atoi(argv[1]);
    *T = atoi(argv[2]);
    *cpuid = atoi(argv[3]);
    pid_t tid = gettid();

    // Print info
    printf("Thread ID: %d\n", tid);
    printf("C: %dms, T: %dms, cpuid: %d\n", *C, *T, *cpuid);

    // Check if the arguments are valid
    if (*C < 0 || *C > 60000 /*ms*/)
    {
        printf("C must be between 0 - 60,000ms\n");
        return -1; // Return error
    }
    else if (*T < 0 || *T > 60000 /*ms*/)
    {
        printf("T must be between 0 - 60,000ms\n");
        return -1; // Return error
    }
    else if (*cpuid < 0)
    {
        printf("cpuid must be greater than or equal to 0\n");
        return -1; // Return error
    }
    return 0; // Success
}

void set_cpu(int cpuid)
{
    // Set the CPU affinity, so the process runs on the specified CPU
    /**
     * sys_sched_setaffinity - set the cpu affinity of a process
     * @pid: pid of the process
     * @len: length in bytes of the bitmask pointed to by user_mask_ptr
     * @user_mask_ptr: user-space pointer to the new cpu mask
     * SYSCALL_DEFINE3(sched_setaffinity, pid_t, pid, unsigned int, len,
                unsigned long __user *, user_mask_ptr)
     */
    unsigned long cpu_mask = 1UL << cpuid;
    if (syscall(__NR_sched_setaffinity, 0, sizeof(cpu_mask), &cpu_mask) < 0)
    {
        // Print error
        printf("CPU %d is offline, turn it on by:\n", cpuid);
        printf("   CPU_PATH=/sys/devices/system/cpu\n");
        printf("   for cpu in 0 1 2 3; do echo 1 > $CPU_PATH/cpu$cpu/online; sleep 1; done\n");
        printf("   for cpu in 0 1 2 3; do echo performance > $CPU_PATH/cpu$cpu/cpufreq/scaling_governor; done\n");
        exit(1); // Return error
    }
}

void sigexcess_handler(int sig)
{
    perror("SIGEXCESS: Exceeded budget\n");
    exit(1);
}

void sigstop_handler(int sig)
{
    printf("SIGSTOP: Stopped\n");
    if (syscall(_NR_end_job) < 0)
    {
        perror("end_job");
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    // Parse the arguments to integers
    int32_t C, T, cpuid;
    if (parse_cmd_args(argc, argv, &C, &T, &cpuid) < 0)
        exit(1);

    // Set the CPU affinity, so the process runs on the specified CPU
    set_cpu(cpuid);

    // Register signal handlers
    signal(SIGEXCESS, sigexcess_handler);
    signal(SIGSTOP, sigstop_handler);

    // Print some extra info
    printf("- Press Ctrl+Z to call end_job()\n");

    struct timeval start, end;
    uint32_t elapsed_ms;
    // Busy-loop for C ms every T ms
    while (1)
    {
        // Periodic task is running on CPU //////////////////////////
        gettimeofday(&start, NULL);
        // Busy-loop for C ms
        while (1)
        {
            gettimeofday(&end, NULL);
            // Calculation of time: (end secs - start secs) * 1000 to get in ms then + (end usecs - start usecs) * 0.001
            elapsed_ms = ((end.tv_sec /*s*/ - start.tv_sec /*s*/) * 1000) /*ms*/ + ((end.tv_usec /*us*/ - start.tv_usec /*us*/) * 0.001) /*ms*/;
            if (elapsed_ms >= C)
                break; // Exit busy-loop
        }
        printf(".");
        fflush(stdout);

        // Periodic task is suspended for T - C ms //////////////////
        // usleep: suspend execution for microsecond intervals
        usleep((T /*ms*/ - C /*ms*/) * 1000 /*us*/); // ms to us
    }

    return 0; // Return success
}