#include "segel.h"
#include "request.h"
#include "log.h"
#include <pthread.h>
#include "request.h"
#include "log.h"
#include <pthread.h>

#define MAX_THREADS 100
#define MAX_QUEUE_SIZE 1000

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

// A synchronized request queue
typedef struct {
    int buffer[MAX_QUEUE_SIZE]; // Circular buffer to hold connfds
    int head;
    int tail;
    int count;                  // Number of items currently in queue
    int size;                   // Max capacity (MAX_QUEUE_SIZE)
    
    // Synchronization primitives
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} RequestQueue;

// Global queue (simplifies access for this example)
RequestQueue q;
server_log log_global; 

// Arguments to pass to worker threads if needed
typedef struct {
    int thread_id;
} thread_arg_t;

void getargs(int *port, int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
}
// TODO: HW3 — Initialize thread pool and request queue
// This server currently handles all requests in the main thread.
// You must implement a thread pool (fixed number of worker threads)
// that process requests from a synchronized queue.

// @@@@@@ Defining the Queue structure
void queue_init(RequestQueue *q, int size) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->size = size;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void enqueue(RequestQueue *q, int connfd) {
    pthread_mutex_lock(&q->mutex);
    
    // If queue is full, wait until a slot opens up
    while (q->count == q->size) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }

    // Add to tail
    q->buffer[q->tail] = connfd;
    q->tail = (q->tail + 1) % q->size;
    q->count++;

    // Signal that the queue is not empty anymore
    pthread_cond_signal(&q->not_empty);
    
    pthread_mutex_unlock(&q->mutex);
}

int dequeue(RequestQueue *q) {
    pthread_mutex_lock(&q->mutex);

    // If queue is empty, wait until a request arrives
    while (q->count == 0) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }

    // Remove from head
    int connfd = q->buffer[q->head];
    q->head = (q->head + 1) % q->size;
    q->count--;

    // Signal that the queue is not full anymore
    pthread_cond_signal(&q->not_full);

    pthread_mutex_unlock(&q->mutex);
    return connfd;
}

void *thread_main(void *arg) {
    thread_arg_t *t_args = (thread_arg_t *)arg;
    
    // Initialize stats for this specific thread
    // Note: We allocate this ONCE per thread, not per request
    threads_stats t_stats = malloc(sizeof(struct Threads_stats));
    t_stats->id = t_args->thread_id;
    t_stats->stat_req = 0;
    t_stats->dynm_req = 0;
    t_stats->total_req = 0;

    time_stats dum; // Dummy time stats if needed by your API

    while (1) {
        // 1. Wait for a request and remove it from queue
        int connfd = dequeue(&q);


        printf("Thread ID %d is handling connection %d\n", t_args->thread_id, connfd);

        // 2. Handle the request
        // The handling logic (parsing HTTP, etc) happens here
        requestHandle(connfd, dum, t_stats, log_global);

        // 3. Close connection
        Close(connfd);
    }

    free(t_args);
    return NULL;
}


int main(int argc, char *argv[])
{
    int listenfd, connfd, port, clientlen;
    struct sockaddr_in clientaddr;
    pthread_t tid[MAX_THREADS];

    getargs(&port, argc, argv);

    // 1. Initialize Log and Queue
    log_global = create_log();
    queue_init(&q, MAX_QUEUE_SIZE);

    // 2. Create Thread Pool
    // Instead of fork(), we use pthread_create
    for(int i = 0; i < MAX_THREADS; i++) {
        thread_arg_t *arg = malloc(sizeof(thread_arg_t));
        arg->thread_id = i;
        pthread_create(&tid[i], NULL, thread_main, (void *)arg);
    }

    listenfd = Open_listenfd(port);

    while (1) {
        clientlen = sizeof(clientaddr);
        
        // This blocks until a client connects
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t*) &clientlen);

        // TODO: HW3 — Record the request arrival time here if needed
        // struct timeval arrival;
        // gettimeofday(&arrival, NULL);

        // 3. Offload work to thread pool
        // Main thread does NOT handle request. It only enqueues.
        enqueue(&q, connfd);
    }
    
    // Cleanup (unreachable in this simple server as while(1) never breaks)
    destroy_log(log_global);
}