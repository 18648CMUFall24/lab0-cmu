#include <stdio.h>
#include <linux/kernel.h>
#include <sys/syscall.h>
#include <unistd.h>

int main()
{
    long int amma = syscall(376);
    printf("System call sys_hello returned %ld\n", amma);
    return 0;
}