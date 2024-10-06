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

// void get_terminal_size(int *rows, int *cols)
// {
//     struct winsize ws;
//     ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
//     *rows = ws.ws_row;
//     *cols = ws.ws_col;
// }


void get_terminal_size(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        // If ioctl() fails, print an error message
        perror("ioctl error");
        *rows = 24;  // Set default values if terminal size can't be obtained
        *cols = 80;
    } else {
        *rows = ws.ws_row;
        *cols = ws.ws_col;

        // Check if rows or cols are 0, meaning the terminal size wasn't retrieved correctly
        if (*rows == 0 || *cols == 0) {
            printf("Warning: Terminal size not detected correctly. Defaulting to 24x80.\n");
            *rows = 24;
            *cols = 80;
        }
    }

    // Print rows and cols for debugging
    printf("Terminal size: rows = %d, cols = %d\n", *rows, *cols);
}

void print_threads(struct rt_thread* list, int loop_len, int rows)
{
    int i = 0;
    int num = 0;
    // Dummy data for process display (replace with actual process data)
    printf("TID      PID      PRIORITY      COMMAND\n");
    printf("-------------------------------------\n");
    // printf("rows = %d\n", rows);
    num = MIN(loop_len, rows - 4);
    for (i = 0; i < num; i++)
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
   printf("\033[H\033[J\033[3J");
}

int compare(const void *a, const void *b) {
    struct rt_thread *t1 = (struct rt_thread *)a;
    struct rt_thread *t2 = (struct rt_thread *)b;
    return t2->priority - t1->priority;     // Sort in descending order
}


int main() {
    int rows, cols, num_to_disp, count;
    int refresh_rate = 2; // refresh rate in 2 seconds
    struct rt_thread rt_threads_list[MAX_THREADS];      // use stack memory

    signal(SIGINT, handle_sigint);
    printf("\033[?25l"); // Hide the cursor

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

        // Call list of threads
        num_to_disp = syscall(SYS_LIST, rt_threads_list, MAX_THREADS);
        if (num_to_disp < 0){
            perror("Error: sys_list failed\n");
            return -1;
        }

        // Print the threads
        qsort(rt_threads_list, num_to_disp, sizeof(struct rt_thread), compare);
        print_threads(rt_threads_list, num_to_disp, rows); 
     
        // Sleep for the refresh rate before updating again
        sleep(refresh_rate);
    }
    // Show the cursor again when the program ends
    printf("\033[?25h");
    return 0;
}