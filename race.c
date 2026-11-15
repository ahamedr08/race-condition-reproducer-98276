#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <linux/sockios.h>

#define DEFAULT_ITERATIONS 1000000
#define BIND_CPU 0
#define IOCTL_CPU 1
#define PROGRESS_INTERVAL 10000

static int shared_fd = -1;
static long iterations = DEFAULT_ITERATIONS;

static pthread_barrier_t start_barrier;
static pthread_barrier_t done_barrier;
static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned long bind_success;
static unsigned long bind_errors;
static unsigned long ioctl_success;
static unsigned long ioctl_enoent;
static unsigned long ioctl_errors;

static void pin_to_cpu(int cpu)
{
    cpu_set_t set;

    CPU_ZERO(&set);
    CPU_SET(cpu, &set);

    int ret = pthread_setaffinity_np(
        pthread_self(), sizeof(set), &set);

    if (ret != 0) {
        fprintf(stderr, "CPU affinity failed for CPU %d: %s\n",
                cpu, strerror(ret));
        exit(EXIT_FAILURE);
    }
}

/* Thread 1: bind() */
static void *bind_worker(void *arg)
{
    pin_to_cpu(BIND_CPU);

    for (long i = 0; i < iterations; i++) {
        pthread_barrier_wait(&start_barrier);

        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = 0,
            .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
        };

        int ret = bind(shared_fd,
                       (struct sockaddr *)&addr,
                       sizeof(addr));

        pthread_mutex_lock(&stats_lock);

        if (ret == 0)
            bind_success++;
        else
            bind_errors++;

        pthread_mutex_unlock(&stats_lock);

        pthread_barrier_wait(&done_barrier);
    }

    return NULL;
}

/* Thread 2: ioctl() */
static void *ioctl_worker(void *arg)
{
    pin_to_cpu(IOCTL_CPU);

    for (long i = 0; i < iterations; i++) {
        pthread_barrier_wait(&start_barrier);

        struct timespec ts;

        int ret = ioctl(shared_fd, SIOCGSTAMPNS_NEW, &ts);
        int saved_errno = errno;

        pthread_mutex_lock(&stats_lock);

        if (ret == 0)
            ioctl_success++;
        else if (saved_errno == ENOENT)
            ioctl_enoent++;
        else
            ioctl_errors++;

        pthread_mutex_unlock(&stats_lock);

        pthread_barrier_wait(&done_barrier);
    }

    return NULL;
}

int main(int argc, char **argv)
{
    if (argc > 1)
        iterations = atol(argv[1]);

    if (iterations <= 0) {
        fprintf(stderr, "Invalid iteration count\n");
        return EXIT_FAILURE;
    }

    printf("Iterations: %ld\n", iterations);

    pthread_t bind_thread;
    pthread_t ioctl_thread;

    pthread_barrier_init(&start_barrier, NULL, 3);
    pthread_barrier_init(&done_barrier, NULL, 3);

    if (pthread_create(&bind_thread, NULL,
                       bind_worker, NULL) != 0) {
        perror("pthread_create bind");
        return EXIT_FAILURE;
    }

    if (pthread_create(&ioctl_thread, NULL,
                       ioctl_worker, NULL) != 0) {
        perror("pthread_create ioctl");
        return EXIT_FAILURE;
    }

    for (long i = 0; i < iterations; i++) {
        shared_fd = socket(AF_INET, SOCK_DGRAM, 0);

        if (shared_fd < 0) {
            perror("socket");
            return EXIT_FAILURE;
        }

        pthread_barrier_wait(&start_barrier);
        pthread_barrier_wait(&done_barrier);

        close(shared_fd);
        shared_fd = -1;

        /* Print only every 10,000 iterations */
        if ((i + 1) % PROGRESS_INTERVAL == 0) {
            printf("Completed iterations: %ld/%ld\n",
                   i + 1, iterations);
            fflush(stdout);
        }
    }

    pthread_join(bind_thread, NULL);
    pthread_join(ioctl_thread, NULL);

/*    printf("\n========== USERSPACE STATISTICS ==========\n");
    printf("Iterations      : %ld\n", iterations);
    printf("Bind successes  : %lu\n", bind_success);
    printf("Bind errors     : %lu\n", bind_errors);
    printf("IOCTL successes : %lu\n", ioctl_success);
    printf("IOCTL ENOENT    : %lu\n", ioctl_enoent);
    printf("IOCTL errors    : %lu\n", ioctl_errors);
*/
    printf("\nRace execution completed.\n");
    printf("Check the BPFtrace output to verify whether the race occurred.\n\n");
    pthread_barrier_destroy(&start_barrier);
    pthread_barrier_destroy(&done_barrier);
    pthread_mutex_destroy(&stats_lock);

    return 0;
}
