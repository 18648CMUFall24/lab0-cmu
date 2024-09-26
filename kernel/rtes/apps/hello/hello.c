/**
 * 4.2 Native Android Application (5 points)
 * Source code location: kernel/rtes/apps/hello/hello.c
 * 
 * Write a small application in C that prints to the console:
 * Hello, world! It's warm and cozy here in user-space
 * Build it (Section 3.3), copy the compiled binary onto the device using adb push, run it in the shell, and
 * check its output.
 * 
 * Commands:
 *      make
 *      adb push hello /data/local/tmp
 *      adb shell
 *      cd /data/local/tmp
 *      (chmod +x hello)
 *      ./hello
 * 
 */

// since it is a user-space application, include stdio.h
#include <stdio.h>

int main() {
    printf("Hello, world! It's warm and cozy here in user-space.\n");
    return 0;
}
