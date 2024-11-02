/**
 * 2.4 Task Monitor Application (15 points + 5 bonus)
 *  Source code location: taskmon/TaskMon/ (build.xml should be in this directory)
 *  Unleash the power of a GUI for your shiny new reservation and instrumentation framework. This
 *  application will serve you well for estimating the resource demands of tasks!
 *
 * 2.4.1 Reservation management UI (5 points)
 *  Create a UI layout which supports setting and canceling reserves by specifying the target thread ID, a budget
 *  C in ms, a period T in ms, and a CPU core ID. Add a selection list with a list of real-time threads running
 *  on the system as an alternative way of specifying the target thread.
 *      > Implemented in 2.2.5 Reservation control application (kernel/rtes/apps/reserve/reserve.c)
 *
 * TA Comments:
 *  - For 2.4.1, you can create the task using C which displays the list of real-time threads and allows the user
 *      to choose from them for setting and cancelling reservations.
 *  - For 2.4.2, you can ask for user input to start and stop the monitoring sessions and subsequently print out
 *      the values of utilization. You can create a persistent terminal interface for this as you mention.
 *  - For 2.4.3, you can plot a graph from the collected data in the monitoring session. The complexity can be
 *      reduced for this by plotting something like a scatter plot (only the points marked with '*' for instance)
 *      instead of drawing the lines and having support for a maximum of 2 or 3 tasks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BUFFER 256
#define ENABLED_FILE "/sys/rtes/taskmon/enabled"

// Read file
int read_file(char *filename, char *buffer)
{
    FILE *file;

    // Open file
    file = fopen(filename, "r");
    if (!file)
    {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return -1;
    }

    // Read file
    if (fgets(buffer, sizeof(buffer), file) == NULL)
    {
        fprintf(stderr, "Failed to read file: %s\n", filename);
        return -1;
    }

    // Close file
    fclose(file);
    return 0; // Success
}

int get_enabled(void)
{
    char buffer[MAX_BUFFER];
    int enabled;
    // Read file
    if (read_file(ENABLED_FILE, buffer) < 0)
        return -1;
    // Convert to integer
    enabled = atoi(buffer);
    return enabled;
}

// Write to enabled file to toggle enabled
void sigquit_handler(int signum)
{
    // Get current status
    int enabled = get_enabled();
    // Toggle status
    enabled = !enabled;
    FILE *file = fopen(ENABLED_FILE, "w");
    if (!file)
    {
        fprintf(stderr, "Failed to open file: %s\n", ENABLED_FILE);
        return;
    }
    // Write to file
    fprintf(file, "%d", enabled);
    // Close file
    fclose(file);
    printf("Status toggled to %s\n", enabled ? "enabled" : "disabled");
}

int main(int argc, char *argv[])
{
    printf("=== Task Monitor ===\n");
    int enabled = get_enabled();
    printf("Status: %s\n", enabled ? "enabled (1)" : "disabled (0)");
    // Toggle status when SIGQUIT is received
    signal(SIGQUIT, sigquit_handler);
    printf("Press Ctrl+\\ to toggle status to %s\n", enabled ? "disabled" : "enabled");

    return 0;
}