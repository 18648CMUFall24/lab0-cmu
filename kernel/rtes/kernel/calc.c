#include <linux/kernel.h>
#include <stdlib.h>
#include <stdio.h>

#define BUFFER_SIZE 16

/**
 * Write a calculator system call (5 points)
 * Source code location: kernel/rtes/kernel/calc.c
 * Add a new system call called calc that takes four input parameters:
 *   - two non-negative rational numbers as character strings,
 *   - an operation (-, +, *, or /) as a character,
 *   - and a buffer to store the result of applying the given arithmetic operation
 *      to the two numbers as a character string.
 *
 * Support a maximum number magnitude of at least 16-bits and a resolution
 * of at least 10-bits after the decimal point.
 * Fail with EINVAL when the result is not a number or the input parameters are not valid.
 *
 * Maximum number magnitude: 16-bits, so ints up to 32,767 (2^15 - 1), which is 5 chars
 * Resolution: at least 10-bits after the decimal point, so 2^10 = 1024 distinct values
 * Which is 3 decimal places (as 1/1024 ≈ 0.0009765625)
 *
 * @param param1 non-negative rational number as a character string
 * @param param2 non-negative rational number as a character string
 * @param operation arithmetic operation (-, +, *, or /)
 * @param result buffer to store the result of applying the given arithmetic operation to the two numbers as a character string
 */
asmlinkage int sys_calc(const char *param1, const char *param2, char operation, char *result)
{
    // Check both parameters are valid
    if (param1 == NULL || param2 == NULL || result == NULL)
        return -EINVAL;
    // Check param is non-negative rational number
    if (param1[0] == '-' || param2[0] == '-')
        return -EINVAL;
    // Check operation is valid
    if (operation != '-' && operation != '+' && operation != '*' && operation != '/')
        return -EINVAL;

    // Convert param1 and param2 to float
    float num1 = atof(param1);
    float num2 = atof(param2);

    // Perform operation
    if (operation == '-')
    {
        res = num1 - num2;
    }
    else if (operation == '+')
    {
        res = num1 + num2;
    }
    else if (operation == '*')
    {
        res = num1 * num2;
    }
    else if (operation == '/')
    {
        if (num2 == 0)
            return -1;
        res = num1 / num2;
    }
    else
    {
        return -1;
    }

    // Write result to buffer, with 4 decimal places
    if (snprintf(result, BUFFER_SIZE, "%.4f", number) < 0)
    {
        // An encoding error occurred
        return -1;
    }

    return 0; // success
}