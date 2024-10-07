#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
    // Open a file without closing it before exiting
    int fd = open("/data/testfile.txt", O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("Failed to open file");
        return 1;
    }
    
    printf("Opened file descriptor: %d\n", fd);

    // Simulate work, but do not close the file
    sleep(2);  // Let the process run a little bit

    // Exit without closing the file
    printf("Exiting without closing the file...\n");
    return 0;
}
