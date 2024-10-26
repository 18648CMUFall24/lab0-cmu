/**
 ** Reservation control application
 * Source code location: kernel/rtes/apps/reserve/reserve.c
 * Write a native userspace application that takes a string command, a thread ID, and command
 * specific arguments and carries out the operation by executing the respective syscall:
 *
 * | command | command-specific arguments                    |
 * |---------|-----------------------------------------------|
 * | set     | budget C in ms, period T in ms, a CPU core ID |
 * | cancel  | none                                          |
 *
 * For example, to set and then to cancel a reserve with a budget of 250 ms and a
 * period of 500 ms on thread with ID 101:
 *     $ ./reserve set 101 250 500 0
 *     $ ./reserve cancel 101
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <asm/unistd.h>
#include <time.h>
#include <linux/unistd.h>
#include <sys/syscall.h>

#define MS_IN_NS 1000000
#define __NR_set_reserve    (379)
#define __NR_cancel_reserve (380)

int parse_cmd_args(int argc, char *argv[], char **cmd, int32_t *tid, int32_t *C, int32_t *T, int32_t *cpuid)
{
    // Parse and check command line arguments
    // Command is required
    if (argc < 2)
    {
        printf("Usage: %s <command> [args]\n", argv[0]);
        return -1;
    }

    // Read the command line arguments
    *cmd = argv[1];
    if (strcmp(*cmd, "set") == 0)
    {
        // Check if the number of arguments is correct
        if (argc != 6)
        {
            printf("Usage: %s set <tid> <C> <T> <cpuid>\n", argv[0]);
            return -1; // Return error
        }
        *tid = atoi(argv[2]);
        *C = atoi(argv[3]);
        *T = atoi(argv[4]);
        *cpuid = atoi(argv[5]);
    }
    else if (strcmp(*cmd, "cancel") == 0)
    {
        // Check if the number of arguments is correct
        if (argc != 3)
        {
            printf("Usage: %s cancel <tid>\n", argv[0]);
            return -1; // Return error
        }
        *tid = atoi(argv[2]);
    }
    else
    {
        printf("%s is not a valid command\n", *cmd);
        printf("Valid commands: set, cancel\n");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    char *cmd;
    int32_t tid, C, T, cpuid;
    struct timespec C_ts, T_ts;

    if (parse_cmd_args(argc, argv, &cmd, &tid, &C, &T, &cpuid) != 0)
    {
        return -1; // Return error
    }
    printf("Args: cmd: %s, tid: %d, C: %dms, T: %dms, cpuid: %d\n", cmd, tid, C, T, cpuid);

    // Call the appropriate syscall based on the command
    if (strcmp(cmd, "set") == 0) {
        // int set_reserve(pid t tid, struct timespec *C, struct timespec *T, int cpuid);
        // Convert C and T to timespec
        C_ts.tv_sec = C / 1000;              // ms -> s
        C_ts.tv_nsec = (C % 1000) * MS_IN_NS; // ms -> ns
    
        T_ts.tv_sec = T / 1000;              // ms -> s
        T_ts.tv_nsec = (T % 1000) * MS_IN_NS; // ms -> ns

        // TODO: replace with syscalls
        printf("set_reserve(tid=%d, C=%ld.%09ld, T=%ld.%09ld, cpuid=%d)\n", tid, C_ts.tv_sec, C_ts.tv_nsec, T_ts.tv_sec, T_ts.tv_nsec, cpuid);
        if (syscall(__NR_set_reserve, tid, &C_ts, &T_ts, cpuid) < 0)
        {
            perror("set_reserve");
            return -1; // Return error
        }
    } else if (strcmp(cmd, "cancel") == 0) {
        // int cancel_reserve(pid t tid);
        // TODO: replace with syscalls
        printf("cancel_reserve(tid=%d)\n", tid);
        if (syscall(__NR_cancel_reserve, tid) < 0)
        {
            perror("cancel_reserve");
            return -1; // Return error
        }
    }

    return 0;
}