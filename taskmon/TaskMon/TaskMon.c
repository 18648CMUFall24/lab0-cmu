/**
 * Demo steps
 * - First need to load kernel module: `insmod ./taskmon.ko`
 *
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
 * 2.4.2 Average utilization display (10 points)
 * To visualize the computation demands of tasks over time, add a UI layout for displaying the average
 * utilization of threads with active reserves. Create an option for starting and stopping a monitoring session
 * (see Section 2.3). After monitoring session is stopped, the UI should compute and display the average
 * utilization of threads with active reserves collected during the session. Threads that have reserves set
 * during the monitoring session should be included.
 * Retrieve the utilization data points from the kernel by reading the sysfs virtual files that you
 * implemented in Section 2.3. List the /sys/rtes/taskmon/util directory to get the list of threads that might have
 * data. You may choose to retrieve the data either periodically during the monitoring session or immediately
 * after the session is stopped.
 * Your app should continue to record data if it is sent to background while the monitoring is on. This
 * allows measuring the utilization of foreground applications (e.g. YouTube decoding thread).
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
#include <signal.h>
#include <dirent.h>

#define MAX_BUFFER 256
#define ENABLED_FILE "/sys/rtes/taskmon/enabled"
#define ENABLED "enabled (1)"
#define DISABLED "disabled (0)"
#define UTIL_DIR "/sys/rtes/taskmon/util"

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
    char buffer[1];
    int enabled;
    // Read file
    if (read_file(ENABLED_FILE, buffer) < 0)
        exit(1);
    // Convert to integer
    enabled = atoi(buffer);
    return enabled;
}

int print_avg_util(void)
{
    DIR *d;
    FILE *file;
    struct dirent *dir;
    char buffer[MAX_BUFFER];
    char abs_filepath[MAX_BUFFER];
    char tids_str[MAX_BUFFER] = "";
    float time_num = 0.0, util_num = 0.0, avg_util = 0.0;
    int i = 0;

    d = opendir(UTIL_DIR);
    if (!d)
    {
        fprintf(stderr, "Failed to open directory: %s\n", UTIL_DIR);
        exit(1);
    }
    while ((dir = readdir(d)) != NULL)
    {
        // Check if it is a regular file, not like a directory or symlink
        if (dir->d_type != DT_REG)
            continue;
        // Check name is a number by comparing the length of the name with the number of digits
        if (strspn(dir->d_name, "0123456789") != strlen(dir->d_name))
            continue;
        strcat(tids_str, dir->d_name); // Append TID to string
        strcat(tids_str, ", ");        // Add comma and space
        // Convert filename into absolute path
        snprintf(abs_filepath, sizeof(abs_filepath), "%s/%s", UTIL_DIR, dir->d_name);
        // Read file content into buffer
        // Open file
        if (!(file = fopen(abs_filepath, "r")))
            fprintf(stderr, "Failed to open file: %s\n", abs_filepath);
        // Read file
        while (fgets(buffer, sizeof(buffer), file) != NULL)
        {
            // Process line: <time> <util>\n
            if (sscanf(buffer, "%f %f\n", &time_num, &util_num) != 2)
            {
                perror("Failed to extract values from util/<tid> file\n");
            }
            // Add util to avg
            avg_util += util_num;
            i++; // Track number of entries
        }

        // Close file
        fclose(file);
    }
    // Take average of util
    avg_util /= i;
    // Print out average utilization and TIDs
    printf("|   %.2f   | %s \n", avg_util, tids_str);
    closedir(d);
    return 0; // Success
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
        exit(1);
    }
    // Write to file
    fprintf(file, "%d", enabled);
    // Close file
    fclose(file);
    printf("Status toggled to %s\n", enabled ? ENABLED : DISABLED);
}
void setup_sigquit_handler()
{
    struct sigaction sa;
    sa.sa_handler = sigquit_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGQUIT, &sa, NULL) == -1)
    {
        perror("Error setting SIGQUIT handler");
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    // Toggle status when SIGQUIT is received
    setup_sigquit_handler();

    printf("====== Task Monitor ======\n");
    int enabled = get_enabled();
    printf("=> Status: %s\n", enabled ? ENABLED : DISABLED);
    printf("=> Press Ctrl+\\ to toggle status to %s\n", enabled ? DISABLED : ENABLED);

    // UI should compute and display the average utilization of threads with active reserves collected during the session.
    printf("| Avg Util | TIDs |\n");
    while (1)
    {
        if (get_enabled() > 0)
        {
            // Only print out avg util and tids if enabled
            print_avg_util();
        }

        sleep(1);
    }

    return 0;
}