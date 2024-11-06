/**
 * Demo steps:
 * ~/lab0-cmu make
 * ~/lab0-cmu/kernel/rtes/apps/calc make
 *
 * Copy file to host and fastboot tablet with new image
 * sudo cp calc /mnt/shared/
 * sudo cp ~/lab0-cmu/arch/arm/boot/zImage /mnt/shared
 * adb devices
 * adb reboot-bootloader
 * fastboot boot zImage boot.img-ramdisk-root.gz
 * adb push calc /data
 * adb shell
 *
 * On target:
 * chmod +x calc
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/syscalls.h>
#include <linux/math64.h>


#define BUFFER_SIZE 32
// Frac bits: 12 (works for */ but errors in +-), 13 (same), 16 (works for +- but not /*)
#define FRACTION_BITS 20
#define FRACTION_SCALE (1 << FRACTION_BITS)

typedef int64_t fixed_point_t;

/**
 * Convert a string to a fixed-point number
 */
fixed_point_t str_to_fixed_point(const char *str) {
    int64_t int_part = 0;       //integer part for the fixed point number
    int64_t frac_part = 0;      //fractional part for the fixed point number
    uint8_t is_neg = 0;         // flag to check if the number is negative even though we don't input negative num
    int i = 0;
    int multiplier = FRACTION_SCALE / 10;   // parsing the frac part as an integer divide by 10
    fixed_point_t fixed_point;
    int decimal_places = 0;

    // Check if number is negative
    if (str[0] == '-')
    {
        is_neg = 1;
        i++;
    } else if (str[0] == '+') {
        i++;    // Skip the + sign
    }

    // Parse integer part
    while (str[i] != '.' && str[i] != '\0') {
        if (str[i] < '0' || str[i] > '9') {
            return -EINVAL; // Invalid character
        }

        int_part = int_part * 10 + (str[i] - '0');
        i++;
    }

    // Parse fractional part
    if (str[i] == '.')
    {
        i++; // Move after the .
        decimal_places = 0;
        while (str[i] != '\0' && multiplier > 0 && decimal_places < 3)
        {
            frac_part += (str[i] - '0') * multiplier;
            multiplier /= 10;
            i++;
            decimal_places++;
        }
    }
    fixed_point = (int_part << FRACTION_BITS) + frac_part;
    return is_neg ? -fixed_point : fixed_point;
}

/**
 * Convert a fixed-point number to a string
 */
void fixed_point_to_str(fixed_point_t fixed_num, char *result, size_t buf_size) {
    int64_t int_part = fixed_num >> FRACTION_BITS;  // integer part (cast to int)
    int64_t frac_part = fixed_num & (FRACTION_SCALE - 1); // fractional part (mask out integer part)
    frac_part = frac_part * 1000 + FRACTION_SCALE / 2; // Round to 3 decimal places
    do_div(frac_part, FRACTION_SCALE);

    if (frac_part == 1000) {
        int_part++;
        frac_part = 0;
    }

    // If the fractional part is zero, print only the integer part
    if (frac_part == 0) {
        snprintf(result, buf_size, "%lld", int_part);  // No decimal part
    } else {
        // Format the number with up to 3 decimal places
        snprintf(result, buf_size, "%lld.%03lld", int_part, frac_part);
    }
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
long do_calc(const char* param1, const char* param2, char operation, char* result) {
    fixed_point_t num1, num2, res;
    int is_neg = 0;

    // Check both parameters are valid
    if (param1 == NULL || param2 == NULL || result == NULL)
        return -EINVAL;
    // Check param is non-negative rational number
    if (param1[0] == '-' || param2[0] == '-')
        return -EINVAL;

    // Convert param1 and param2 to float
    num1 = str_to_fixed_point(param1);
    num2 = str_to_fixed_point(param2);

    // Perform operation
    switch (operation) {
    case '+':
        res = num1 + num2;
        break;
    case '-':
        res = num1 - num2;
        break;
    case '*':
        res = (num1 * num2) >> FRACTION_BITS;
        break;
    case '/':
        if (num2 == 0) return -EINVAL; // Division by zero

        res = div64_s64((num1<<FRACTION_BITS), num2);
        break;
    default:
        return -EINVAL; // Invalid operation
    }

    // Write result to buffer
    if (res < 0) {
        is_neg = 1;
        res = -res;
    }

    if (is_neg) {
        snprintf(result, BUFFER_SIZE, "-");
        fixed_point_to_str(res, result + 1, BUFFER_SIZE - 1);
    } else {
        fixed_point_to_str(res, result, BUFFER_SIZE);
    }

    return 0; // success
}

SYSCALL_DEFINE4(calc, const char*, param1, const char*, param2, char, operation, char*, result) {
    return do_calc(param1, param2, operation, result);
}

EXPORT_SYMBOL(do_calc);