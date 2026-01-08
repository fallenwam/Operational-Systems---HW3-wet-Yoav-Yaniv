#include <stdlib.h>
#include <string.h>
#include "log.h"
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

// Opaque struct definition
struct Server_Log
{
    char *buffer;
    int size;
    int capacity;
    int sleep;

    pthread_mutex_t mutex;
    pthread_cond_t cond_readers;
    pthread_cond_t cond_writers;

    int readers_count;   // how many currently reading
    bool writers_active; // is a writer writing
    int writers_waiting; // how many writers waiting
};

// Creates a new server log instance (stub)
server_log create_log(int sleepTime)
{
    server_log log = (server_log)malloc(sizeof(struct Server_Log));
    if (log == NULL)
    {
        perror("log malloc failed in create_log");
        exit(0);
    } // malloc failed

    log->capacity = 1024;
    log->buffer = (char *)malloc(log->capacity);

    if (log->buffer == NULL)
    {
        free(log);
        perror("buffer malloc failed in create_log");

        exit(0);
    } // malloc failed

    log->buffer[0] = '\0'; // start with empty string
    log->size = 0;
    log->readers_count = 0;
    log->writers_active = false;
    log->writers_waiting = 0;
    if (sleepTime <= 0)
    {
        log->sleep = 0;
    }
    else
    {
        log->sleep = sleepTime;
    }

    pthread_mutex_init(&log->mutex, NULL);
    pthread_cond_init(&log->cond_readers, NULL);
    pthread_cond_init(&log->cond_writers, NULL);
    return log;
}

// Destroys and frees the log (stub)
void destroy_log(server_log log)
{
    if(log == NULL){
        return;
    }
    pthread_mutex_destroy(&log->mutex);
    pthread_cond_destroy(&log->cond_readers);
    pthread_cond_destroy(&log->cond_writers);
    free(log->buffer);
    free(log);
}

// Returns dummy log content as string (stub)
int get_log(server_log log, char **dst)
{
    if (log == NULL || dst == NULL)
        return -1;

    char *copy = (char *)malloc(log->size + 1);
    if (copy == NULL)
    {//malloc failed
        perror("malloc failed in get_log");
        return -1;
    }

    memcpy(copy, log->buffer, log->size);
    if (log->sleep > 0)
    {
        sleep(log->sleep);
    }
    copy[log->size] = '\0';
    *dst = copy;
    return log->size;
}

void reader_lock(server_log log){
    if (log == NULL)
        return;

    pthread_mutex_lock(&log->mutex);

    while (log->writers_active || log->writers_waiting > 0)
    {
        pthread_cond_wait(&log->cond_readers, &log->mutex);
    }

    log->readers_count++;
    pthread_mutex_unlock(&log->mutex);
}

void reader_unlock(server_log log){
    if (log == NULL)
        return;

    pthread_mutex_lock(&log->mutex);
    log->readers_count--;

    if (log->readers_count == 0)
    {
        pthread_cond_signal(&log->cond_writers);
    }

    pthread_mutex_unlock(&log->mutex);
}

void add_to_log(server_log log, const char *data, int data_len)
{
    if (log == NULL || data == NULL)
        return;

    //  Resize Buffer if needed
    int required_size = log->size + data_len + 1; // +1 for null terminator
    if (required_size > log->capacity)
    {
        // Double the capacity (or set to the required size)
        int new_capacity = log->capacity * 2;
        if (new_capacity < required_size)
            new_capacity = required_size;

        char *new_buf = realloc(log->buffer, new_capacity);
        if (new_buf == NULL)
        {
            // If realloc fails, exit destroys threads so no need for unlock
            perror("realloc failed in add_to_log");
            exit(-1);
        }
        log->buffer = new_buf;
        log->capacity = new_capacity;
    }

    memcpy(log->buffer + log->size, data, data_len);
    log->size += data_len;
    log->buffer[log->size] = '\0';
    if (log->sleep > 0)
    {
        sleep(log->sleep);
    }
}

int get_log_sleep(server_log log){
    return log->sleep;
}

void writer_lock(server_log log){
    if (log == NULL)
        return;

    pthread_mutex_lock(&log->mutex);

    log->writers_waiting++;
    while (log->readers_count > 0 || log->writers_active)
    {
        pthread_cond_wait(&log->cond_writers, &log->mutex);
    } // add to waiting queue

    log->writers_waiting--;
    log->writers_active = true;
}

void writer_unlock(server_log log){
    if (log == NULL)
        return;

    log->writers_active = false;

    if (log->writers_waiting > 0)
    {
        pthread_cond_signal(&log->cond_writers);
    }
    else
    {
        pthread_cond_broadcast(&log->cond_readers);
    }

    pthread_mutex_unlock(&log->mutex);
}
