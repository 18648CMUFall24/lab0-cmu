/**
 * 4.6 RTPS (20 points + 5 bonus)
 * Source code location: kernel/rtes/apps/rtps/rtps.c
 * Write an application called rtps that displays a list of real-time threads
 * that exist on the device in descending order of real-time priority.
 * Make use of your syscalls created in Section 4.5.4. The application must not quit
 * until you hit Ctrl-C and must print updated information every 2 seconds.
 * See Section 3.3 for building.

 * 4.6.1 Bonus: persistent terminal interface (5 points)
 * The terminal screen must not scroll each time it updates. The updated information
 * should cleanly overwrite the old information, much like the program top.
 * Only as many processes as fit into the current terminal size should be displayed.
 */

#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <linux/termios.h>
#include <signal.h>

#define SYS_COUNT 377 // count_rt_threads
#define SYS_LIST 378 // list_rt_threads
#define MAX_THREADS 200

#define MIN(a,b) (((a)<(b))?(a):(b))

struct rt_thread
{
    pid_t tid;    /* Thread ID */
    pid_t pid;    /* Process ID */
    int priority; /* Thread Priority */
    char name[20];   /* Name (command) */
};

void get_terminal_size(int *rows, int *cols)
{
    struct winsize ws;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    *rows = ws.ws_row;
    *cols = ws.ws_col;
}

void print_threads(struct rt_thread* list, int loop_len)
{
    printf("HERE: 2\n");

    int i = 0;
    // Dummy data for process display (replace with actual process data)
    printf("TID   PID   PRIORITY     COMMAND\n");
    printf("----  ----  --------     -------\n");
    for (i = 0; i < loop_len; i++)
    {
        // printf("%6d  %6d   %4d   %s\n",
        //         list[i]->tid, list[i]->pid, list[i]->priority, list[i]->name);
        printf("%6d  %6d   %4d   %s\n",
                list[i].tid, list[i].pid, list[i].priority, list[i].name);
    }
}

void handle_sigint(int sig)
{
    // Show the cursor again when the program ends
    printf("\033[?25h");
    exit(0);
}

void clear_screen() {
    // Clear the screen
    printf("\033[2J");
    // Move the cursor to the top of the terminal
    printf("\033[H");
}

int compare(const void *a, const void *b) {
    struct rt_thread *t1 = (struct rt_thread *)a;
    struct rt_thread *t2 = (struct rt_thread *)b;
    return t2->priority - t1->priority;     // Sort in descending order
}


int main() {
    int rows, cols, num_to_disp, count;
    int refresh_rate = 2; // refresh rate in 2 seconds
    struct rt_thread rt_threads_list[MAX_THREADS];

    signal(SIGINT, handle_sigint);
    // Hide the cursor to make the display cleaner
    // printf("\033[?25l");

    while (1) {
        clear_screen();
        // Get current terminal size
        get_terminal_size(&rows, &cols);
        // Move the cursor to the top of the terminal without clearing the content
        // printf("\033[H");
        count = syscall(SYS_COUNT);
        if (count < 0) {
            printf("Error: Unable to get real-time thread count\n");
            break;
        }
        printf("Number of real-time threads: %d\n", count);
        
        // // num_to_disp = MIN(rows, count);
        // num_to_disp = count;
        // if (num_to_disp <= 0)
        // {
        //     printf("Error: Terminal size too small to display any threads\n");
        //     sleep(refresh_rate);
        //     continue;
        // }

        // Call list of threads
        num_to_disp = syscall(SYS_LIST, rt_threads_list, MAX_THREADS);
        if (num_to_disp < 0){
            perror("Error: sys_list failed\n");
            // free(rt_threads_list);
            return -1;
        }

        // printf("HERE: 1\n");
        // printf("rt_threads_list=%p\n", rt_threads_list);
        // printf("OR rt_threads_list=%p\n", &rt_threads_list);
        // Print the threads
        qsort(rt_threads_list, num_to_disp, sizeof(struct rt_thread), compare);
        print_threads(rt_threads_list, num_to_disp); 
        // printf("HERE: 10\n");

     
        // Sleep for the refresh rate before updating again
        sleep(refresh_rate);
    }
    // Show the cursor again when the program ends
    // printf("\033[?25h");
    return 0;
}