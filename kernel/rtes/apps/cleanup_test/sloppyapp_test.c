#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/prctl.h>

int main() {
    // Set the process name to "sloppyapp"
    prctl(PR_SET_NAME, "sloppyapp", 0, 0, 0);

    // Open a file without closing it before exiting
    int fd = open("/data/local/tmp/testfile.txt", O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("Failed to open file");
        return 1;
    }
    write(fd, "Hello, world!\n", 14);
    printf("Opened file descriptor: %d\n", fd);

    // Simulate some work
    sleep(2);

    // Exit without closing the file
    printf("Exiting without closing the file...\n");
    return 0;
}
