#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/syscall.h>

#define SYS_COUNT 377
#define SYS_LIST 378

struct rt_thread
{
    pid_t tid;    /* Thread ID */
    pid_t pid;    /* Process ID */
    int priority; /* Thread Priority */
    char *name;   /* Name (command) */
};

int main() {
    int i = 0;
    long count = syscall(SYS_COUNT);
    if (count < 0) {
        printf("Error: Unable to get real-time thread count\n");
        return -1;
    }
    printf("Number of real-time threads: %ld\n", count);

    unsigned int num_threads = 50;
    struct rt_thread *rt_threads_list = malloc(count * sizeof(struct rt_thread));

    if (syscall(SYS_LIST, rt_threads_list, num_threads) < 0) {
        printf("Error: Unable to get real-time thread list\n");
        free(rt_threads_list);
        return -1;
    }

    printf("TID   PID   PRIORITY     COMMAND\n");
    for (i = 0; i < num_threads; i++) {
        printf("%6d  %6d   %4d   %s\n",
               rt_threads_list[i].tid, rt_threads_list[i].pid,
               rt_threads_list[i].priority, rt_threads_list[i].name);
    }
    free(rt_threads_list);
    return 0;
}