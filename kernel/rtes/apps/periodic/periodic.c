/**
 * Source code location: kernel/rtes/apps/periodic/periodic.c
 *  Write a native userspace application that takes C, T, cpuid arguments on the command line and busy-
 *  loops for C µs every T µs on the CPU cpuid. Support C and T values up to 60,000 ms (60 secs). The app
 *  should be runnable on the stock kernel too: it does not rely on any of your modifications to the kernel.
 *  Use this app to test your budget accounting and task monitor.
 *
 * For example, to create a periodic task that performs 250 ms of computation every 500 ms on CPU 0:
 *      $ ./periodic 250 500 0
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sched.h>

int main(int argc, char *argv[])
{
    // Check if the number of arguments is correct
    if (argc != 4)
    {
        printf("Usage: %s <C> <T> <cpuid>\n", argv[0]);
        return 1; // Return error
    }

    // Parse the arguments to integers
    int C = atoi(argv[1]);
    int T = atoi(argv[2]);
    int cpuid = atoi(argv[3]);
    printf("C: %d, T: %d, cpuid: %d\n", C, T, cpuid);

    // Check if the arguments are valid
    if (C < 0 || C > 60000)
    {
        printf("C must be between 0 - 60,000ms\n");
        return 1; // Return error
    }
    else if (T < 0 || T > 60000)
    {
        printf("T must be between 0 - 60,000ms\n");
        return 1; // Return error
    }
    else if (cpuid < 0)
    {
        printf("cpuid must be greater than or equal to 0\n");
        return 1; // Return error
    }

    // Set the CPU affinity, so the process runs on the specified CPU
    unsigned long cpu_mask = 1 << cpuid;
    if (sched_setaffinity(0, sizeof(cpu_mask), &cpu_mask) < 0)
    {
        perror("sched_setaffinity");
        return 1;
    }

    struct timeval start, end;
    int32_t elapsed_ms;
    // Busy-loop for C µs every T µs
    while (1)
    {
        // Periodic task is running on CPU //////////////////////////
        gettimeofday(&start, NULL);
        // Busy-loop for C µs
        while (1)
        {
            gettimeofday(&end, NULL);
            // Calculation of time: (end secs - start secs) * 1000000 to get in ms then + (end usecs - start usecs)
            elapsed_ms = ((end.tv_sec - start.tv_sec) * 1000000) + (end.tv_usec - start.tv_usec);
            if (elapsed_ms >= C)
                break; // Exit busy-loop
        }
        printf(".");

        // Periodic task is suspended for T - C µs //////////////////
        // usleep: suspend execution for microsecond intervals
        usleep(T - C);
    }

    return 0; // Return success
}