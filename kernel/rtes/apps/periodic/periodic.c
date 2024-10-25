/**
 * Source code location: kernel/rtes/apps/periodic/periodic.c
 *  Write a native userspace application that takes C, T, cpuid arguments on the command line and busy-
 *  loops for C µs every T µs on the CPU cpuid. Support C and T values up to 60,000 ms (60 secs). The app
 *  should be runnable on the stock kernel too: it does not rely on any of your modifications to the kernel.
 *  Use this app to test your budget accounting and task monitor. For example, to create a periodic task
 *  that performs 250 ms of computation every 500 ms on CPU 0:
 *
 * $ ./periodic 250 500 0
 */