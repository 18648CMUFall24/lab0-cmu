// compute_program.c

#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <linux/signal.h>

#define SIGEXCESS (33)

void sigexcess_handler(int signo, siginfo_t *info, void *context)
{
    printf("Received SIGEXCESS (signal %d). Budget overrun detected.\n", signo);
    printf("Overrun amount: %d ns\n", info->si_int);
    // Handle the budget overrun (e.g., adjust computation, log event, etc.)
}

void setup_sigexcess_handler()
{
    struct sigaction sa;
    sa.sa_sigaction = sigexcess_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGEXCESS, &sa, NULL) == -1) {
        perror("sigaction");
        _exit(1);
    }
}

void* compute(void* arg)
{
    setup_sigexcess_handler();

    volatile unsigned long long i = 0;
    while (1) {
        // Simulate computation
        i++;
        if (i % 100000000 == 0) {
            printf("Thread %ld is computing...\n", pthread_self());
            sleep(1); // Optional: Sleep to simulate varying workload
        }
    }
    return NULL;
}

int main()
{
    pthread_t thread;
    pthread_create(&thread, NULL, compute, NULL);

    pthread_join(thread, NULL);

    return 0;
}
