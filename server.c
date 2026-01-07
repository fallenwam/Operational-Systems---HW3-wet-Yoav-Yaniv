#include "segel.h"
#include "request.h"
#include "log.h"
#include <pthread.h>
#include "request.h"
#include "log.h"
#include <sys/resource.h>
#include <limits.h>

int get_max_thread_limit()
{
    struct rlimit rl;

    // RLIMIT_NPROC: The max number of processes (threads on Linux)
    // for the real user ID of the calling process.
    if (getrlimit(RLIMIT_NPROC, &rl) == 0)
    {
        return (int)rl.rlim_max;
    }
    return INT_MAX;
}
//
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// Parses command-line arguments

typedef struct{
    int connfd;
    time_stats time;
} QueueElement;

typedef struct {
    QueueElement* buffer; // Circular buffer to hold connfds
    int head;
    int tail;
    int count;                  // Number of items currently in queue
    int size;                   // Max capacity (MAX_QUEUE_SIZE)

    // Synchronization primitives
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} RequestQueue;

// Global queue and log
RequestQueue q;
server_log log_global;

// Arguments to pass to worker threads if needed
typedef struct {
    int thread_id;
} thread_arg_t;

void getargs(int *port, int *threads, int *queue_size, int *debug_sleep,
             int argc, char *argv[])
{
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <portnum> <threads> <queue_size> "
                        "<debug_sleep_time>\n",
                argv[0]);
        exit(0);
    }
    *port = atoi(argv[1]);
    *threads = atoi(argv[2]);
    *queue_size = atoi(argv[3]);
    *debug_sleep = atoi(argv[4]);
}

void queue_init(RequestQueue *q, int size) {
    q->buffer = malloc(sizeof(QueueElement) * size);
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->size = size;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void enqueue(RequestQueue *q, int connfd, struct timeval arrival) {
    pthread_mutex_lock(&q->mutex);

    // If queue is full, wait until a slot opens up
    while (q->count == q->size) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }

    // Add to tail
    q->buffer[q->tail].connfd = connfd;
    q->buffer[q->tail].time.task_arrival = arrival;
    q->tail = (q->tail + 1) % q->size;
    q->count++;

    // Signal that the queue is not empty anymore
    pthread_cond_signal(&q->not_empty);

    pthread_mutex_unlock(&q->mutex);
}

QueueElement dequeue(RequestQueue *q) {
    pthread_mutex_lock(&q->mutex);

    // If queue is empty, wait until a request arrives
    while (q->count == 0) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }

    // Remove from head
    QueueElement element = q->buffer[q->head];
    q->head = (q->head + 1) % q->size;
    q->count--;

    gettimeofday(&element.time.task_dispatch,NULL);

    // Signal that the queue is not full anymore
    pthread_cond_signal(&q->not_full);

    pthread_mutex_unlock(&q->mutex);
    return element;
}

void *thread_main(void *arg) {
    thread_arg_t *t_args = (thread_arg_t *)arg;

    // Initialize stats for this specific thread
    // Note: We allocate this ONCE per thread, not per request
    threads_stats t_stats = malloc(sizeof(struct Threads_stats));
    if (t_stats == NULL)
    {
        exit(0);
    }
    t_stats->id = t_args->thread_id;
    t_stats->stat_req = 0;
    t_stats->dynm_req = 0;
    t_stats->total_req = 0;
    t_stats->post_req = 0;

    while (1) {
        // 1. Wait for a request and remove it from queue
        QueueElement request = dequeue(&q);

        printf("Thread ID %d is handling connection %d\n", t_args->thread_id, request.connfd);

        // 2. Handle the request
        // The handling logic (parsing HTTP, etc) happens here
        requestHandle(request.connfd, request.time, t_stats, log_global);

        // 3. Close connection
        Close(request.connfd);
    }

    free(t_args);
    return NULL;
}

int main(int argc, char *argv[])
{
    int listenfd, connfd, port, clientlen, max_thread, queue_size, debug_sleep;
    struct sockaddr_in clientaddr;

    getargs(&port, &max_thread, &queue_size, &debug_sleep, argc, argv);

    if (port <= 1023 || 65535 < port || max_thread < 1 || 
        get_max_thread_limit() < max_thread || queue_size < 1)
    {
        exit(0);
    }
    pthread_t *tid = (pthread_t *)malloc(sizeof(pthread_t) * max_thread);
    if(tid == NULL){
        exit(0);
    }
    // 1. Initialize Log and Queue
    log_global = create_log(debug_sleep);
    queue_init(&q, queue_size);

    // 2. Create Thread Pool
    // Instead of fork(), we use pthread_create
    for(int i = 0; i < max_thread; i++) {
        thread_arg_t *arg = malloc(sizeof(thread_arg_t));
        if (arg == NULL)
        {
            exit(0);
        }
        arg->thread_id = i + 1;
        pthread_create(&tid[i], NULL, thread_main, (void *)arg);
    }

    listenfd = Open_listenfd(port);

    while (1) {
        clientlen = sizeof(clientaddr);
        struct timeval arrival;

        // This blocks until a client connects

        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t*) &clientlen);
        gettimeofday(&arrival, NULL);

        // 3. Offload work to thread pool
        // Main thread does NOT handle request. It only enqueues.
        enqueue(&q, connfd, arrival);
    }

    // Cleanup (unreachable in this simple server as while(1) never breaks)
    destroy_log(log_global);
}