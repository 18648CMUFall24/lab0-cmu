#include <stdio.h>
#include <linux/kernel.h>
#include <sys/syscall.h>

#define BUFFER_SIZE 16
#define __NR_calc 376

/**
 * 4.5.2 Write a calculator application (5 points)
 * Source code location: kernel/rtes/apps/calc/calc.c
 * Write a user-level application that takes two non-negative rational numbers and
 * a character operation from the command line (escaped with single quotes when needed),
 * makes the calc system call, and outputs to stdout nothing but the result as a number
 * in base 10 or nan:
 * ```
 * 1 $ ./calc 3 + 0.14
 * 2 3.14
 * 3 $ ./calc 3.14 / 0
 * 4 nan
 * ```
 */
int main(int argc, char *argv[])
{
    // Eg. argv = ["./calc", "3", "+", "0.14"]
    if (argc != 4)
    {
        // Invalid number of arguments
        printf("Usage: %s <param1> <operation> <param2>\n", argv[0]);
        printf("# of params received: %d\n", argc);
        printf("Note: For multiplication, use '*' in quotes or escape it with \\*\n");

        return -1;
    }

    char *param1 = argv[1];
    char *param2 = argv[3];
    char operation = argv[2][0];
    char result[BUFFER_SIZE];

    long ret_val = syscall(__NR_calc, param1, param2, operation, result);
    if (ret_val == 0)
        printf("%s\n", result);
    else
        printf("nan\n");

    return 0;
}