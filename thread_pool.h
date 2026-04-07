#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>

typedef struct {
    void (*handler)(void*);
    void* arg;
} task_t;

typedef struct {
    pthread_t* threads;
    task_t* tasks;
    int thread_count;
    int queue_size;
    int head;
    int tail;
    int count;
    int shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_cond_t done;
} thread_pool_t;

int thread_pool_init(thread_pool_t* pool, int thread_count, int queue_size);
void thread_pool_submit(thread_pool_t* pool, void (*handler)(void*), void* arg);
void thread_pool_shutdown(thread_pool_t* pool);

#endif
