#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>

#define BUFFER_SIZE 16
#define FRACTION_BITS 16
#define FRACTION_SCALE (1 << FRACTION_BITS)

typedef long fixed_point_t;

/**
 * Convert a string to a fixed-point number
 */
fixed_point_t str_to_fixed_point(const char *str)
{
    long int_part = 0;
    long frac_part = 0;
    int is_neg = 0;
    int i = 0;
    long multiplier = FRACTION_SCALE / 10;
    fixed_point_t fixed_point;

    // Check if number is negative
    if (str[0] == '-')
    {
        is_neg = 1;
        i++;
    }

    // Parse integer part
    while (str[i] != '.' && str[i] != '\0')
    {
        int_part = int_part * 10 + (str[i] - '0');
        i++;
    }

    // Parse fractional part
    if (str[i] == '.')
    {
        i++;
        while (str[i] != '\0' && multiplier > 0)
        {
            frac_part += (str[i] - '0') * multiplier;
            multiplier /= 10;
            i++;
        }
    }
    fixed_point = (int_part << FRACTION_BITS) + frac_part;
    return is_neg ? -fixed_point : fixed_point;
}

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
    fixed_point_t num1, num2, res;
    int is_neg;
    long int_part, frac_part;

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
    num1 = str_to_fixed_point(param1);
    num2 = str_to_fixed_point(param2);

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
        res = (num1 * num2) >> FRACTION_BITS;
    }
    else if (operation == '/')
    {
        if (num2 == 0)
            return -1;
        res = (num1 << FRACTION_BITS) / num2;
    }
    else
    {
        return -1;
    }

    // Write result to buffer, with 4 decimal places
    is_neg = res < 0;
    if (is_neg)
        res = -res;

    int_part = res >> FRACTION_BITS;
    frac_part = ((res & (FRACTION_SCALE - 1)) * 1000000) >> FRACTION_BITS;

    if (snprintf(result, BUFFER_SIZE, "%s%ld.%06ld", is_neg ? "-" : "", int_part, frac_part) < 0)
    {
        return -1; // error
    }

    return 0; // success
}
