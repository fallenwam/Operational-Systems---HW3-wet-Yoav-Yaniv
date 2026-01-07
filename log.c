#include <stdlib.h>
#include <string.h>
#include "log.h"
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

// Opaque struct definition
struct Server_Log
{
    // TODO: Implement internal log storage (e.g., dynamic buffer, linked list, etc.)
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
        exit(0);
    } // malloc failed

    log->capacity = 1024;
    log->buffer = (char *)malloc(log->capacity);

    if (log->buffer == NULL)
    {
        free(log);
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
    free(log->buffer);
    free(log);
}

// Returns dummy log content as string (stub)
int get_log(server_log log, char **dst)
{
    // TODO: Return the full contents of the log as a dynamically allocated string
    // This function should handle concurrent access

    if (log == NULL || dst == NULL)
        return -1;

    pthread_mutex_lock(&log->mutex);

    while (log->writers_active || log->writers_waiting > 0)
    {
        pthread_cond_wait(&log->cond_readers, &log->mutex);
    }

    log->readers_count++;
    pthread_mutex_unlock(&log->mutex);

    char *copy = (char *)malloc(log->size + 1);
    if (copy == NULL)
    {
        pthread_mutex_lock(&log->mutex);
        log->readers_count--;
        if (log->readers_count == 0 || log->writers_waiting > 0)
        {
            pthread_cond_signal(&log->cond_writers);
        }
        pthread_mutex_unlock(&log->mutex);
        return -1;
    }

    memcpy(copy, log->buffer, log->size);
    if (log->sleep > 0)
    {
        sleep(log->sleep);
    }
    *dst = copy;
    pthread_mutex_lock(&log->mutex);
    log->readers_count--;

    if (log->readers_count == 0 || log->writers_waiting > 0)
    {
        pthread_cond_signal(&log->cond_writers);
    }

    pthread_mutex_unlock(&log->mutex);
    return log->size;
}

// Appends a new entry to the log (no-op stub)
void add_to_log(server_log log, const char *data, int data_len)
{
    // TODO: Append the provided data to the log
    // This function should handle concurrent access

    if (log == NULL || data == NULL)
        return;

    pthread_mutex_lock(&log->mutex);

    log->writers_waiting++;
    while (log->readers_count > 0 || log->writers_active)
    {
        pthread_cond_wait(&log->cond_writers, &log->mutex);
    } // add to waiting queue

    log->writers_waiting--;
    log->writers_active = true;

    //    possibly uncomment
    //    pthread_mutex_unlock(&log->mutex);

    // TODO: go over this section:
    //  5. Resize Buffer if needed (Critical Fix)
    int required_size = log->size + data_len + 1; // +1 for null terminator
    if (required_size > log->capacity)
    {
        // Double the capacity (or set to required if doubling isn't enough)
        int new_capacity = log->capacity * 2;
        if (new_capacity < required_size)
            new_capacity = required_size;

        char *new_buf = realloc(log->buffer, new_capacity);
        if (new_buf == NULL)
        {
            // If realloc fails, we are in trouble.
            // For now, clean up state and exit to avoid deadlock
            log->writers_active = 0;
            // Signal others so they aren't stuck forever
            if (log->writers_waiting > 0)
                pthread_cond_signal(&log->cond_writers);
            else
                pthread_cond_broadcast(&log->cond_readers);
            pthread_mutex_unlock(&log->mutex);
            return;
        }
        log->buffer = new_buf;
        log->capacity = new_capacity;
    }

    if (log->sleep > 0)
    {
        sleep(log->sleep);
    }
    // last chance to updade the dispatch
    memcpy(log->buffer + log->size, data, data_len);
    log->size += data_len;
    log->buffer[log->size] = '\0';

    //    possibly uncomment
    //    pthread_mutex_lock(&log->mutex);
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

int get_log_sleep(server_log log){
    return log->sleep;
}
