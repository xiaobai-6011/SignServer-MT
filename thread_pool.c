#include "thread_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void* worker(void* arg) {
    thread_pool_t* pool = (thread_pool_t*)arg;
    
    while (1) {
        pthread_mutex_lock(&pool->mutex);
        
        while (pool->count == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }
        
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        
        task_t task = pool->tasks[pool->head];
        pool->head = (pool->head + 1) % pool->queue_size;
        pool->count--;
        
        pthread_mutex_unlock(&pool->mutex);
        
        task.handler(task.arg);
    }
    
    return NULL;
}

int thread_pool_init(thread_pool_t* pool, int thread_count, int queue_size) {
    pool->thread_count = thread_count;
    pool->queue_size = queue_size;
    pool->head = 0;
    pool->tail = 0;
    pool->count = 0;
    pool->shutdown = 0;
    
    pool->tasks = (task_t*)malloc(sizeof(task_t) * queue_size);
    pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * thread_count);
    
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);
    pthread_cond_init(&pool->done, NULL);
    
    for (int i = 0; i < thread_count; i++) {
        pthread_create(&pool->threads[i], NULL, worker, pool);
    }
    
    return 0;
}

void thread_pool_submit(thread_pool_t* pool, void (*handler)(void*), void* arg) {
    pthread_mutex_lock(&pool->mutex);
    
    pool->tasks[pool->tail].handler = handler;
    pool->tasks[pool->tail].arg = arg;
    pool->tail = (pool->tail + 1) % pool->queue_size;
    pool->count++;
    
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
}

void thread_pool_shutdown(thread_pool_t* pool) {
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
    
    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    pthread_cond_destroy(&pool->done);
    
    free(pool->tasks);
    free(pool->threads);
}
