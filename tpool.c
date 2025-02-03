#include "tpool.h"

#include <assert.h>
#include <bits/pthreadtypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

static inline void swap(int *a, int *b) {
    int t = *a;
    *a = *b;
    *b = t;
}

void* frontend_job(void *void_pool) {
    struct tpool *pool = (struct tpool*) void_pool;
    pthread_mutex_lock(&pool->frontend_queue_mutex);
    while (1) {
        while (pool->frontend_queue.size == 0) {
            pthread_cond_wait(&pool->frontend_queue_cond, &pool->frontend_queue_mutex);
        }
        if (pool->frontend_queue.size == -1) {
            pthread_mutex_unlock(&pool->frontend_queue_mutex);
            pthread_exit(NULL);
        }
        while (pool->frontend_queue.size > 0) {
            struct frontend_job current_job = FqFront(&pool->frontend_queue);
            FqPop(&pool->frontend_queue);

            pthread_mutex_lock(&pool->working_threads_mutex);
            pool->num_working_threads++;
            pthread_mutex_unlock(&pool->working_threads_mutex);
            pthread_mutex_unlock(&pool->frontend_queue_mutex);
            pthread_cond_broadcast(&pool->frontend_queue_cond);

            for (int row = 0; row < pool->matrix_size; ++row) {
                for (int col = row; col < pool->matrix_size; ++col) {
                    swap(&(current_job.b[row][col]), &(current_job.b[col][row]));
                }
            }

            int work_size = (pool->matrix_size * pool->matrix_size) / current_job.num_works;
            int num_one_more_job = (pool->matrix_size * pool->matrix_size) % current_job.num_works;
            int start = 0;
            pthread_mutex_lock(&pool->backend_queue_mutex);
            for (int i = 0; i < num_one_more_job; ++i) {
                struct backend_job new_job = {
                    .a = current_job.a, .b = current_job.b, .c = current_job.c,
                    .start_id = start, .end_id = start + work_size + 1
                };
                BqPush(&pool->backend_queue, new_job);
                start += work_size + 1;
            }
            pthread_cond_broadcast(&pool->backend_queue_cond);
            pthread_mutex_unlock(&pool->backend_queue_mutex);
            
            pthread_mutex_lock(&pool->backend_queue_mutex);
            for (int i = 0; i < current_job.num_works - num_one_more_job; ++i) {
                struct backend_job new_job = {
                    .a = current_job.a, .b = current_job.b, .c = current_job.c,
                    .start_id = start, .end_id = start + work_size
                };
                BqPush(&pool->backend_queue, new_job);
                start += work_size;
            }
            assert(start == pool->matrix_size * pool->matrix_size);
            pthread_cond_broadcast(&pool->backend_queue_cond);
            pthread_mutex_unlock(&pool->backend_queue_mutex);

            pthread_mutex_lock(&pool->frontend_queue_mutex);
            pthread_mutex_lock(&pool->working_threads_mutex);
            pool->num_working_threads--;
            pthread_cond_broadcast(&pool->working_threads_cond);
            pthread_mutex_unlock(&pool->working_threads_mutex);
        }
    }
    pthread_exit(NULL);
}

void* backend_job(void *void_pool) {
    struct tpool *pool = (struct tpool*) void_pool;
    pthread_mutex_lock(&pool->backend_queue_mutex);
    while (1) {
        while (pool->backend_queue.size == 0) {
            pthread_cond_wait(&pool->backend_queue_cond, &pool->backend_queue_mutex);
        }
        if (pool->backend_queue.size == -1) {
            pthread_mutex_unlock(&pool->backend_queue_mutex);
            pthread_exit(NULL);
        }
        while (pool->backend_queue.size > 0) {
            struct backend_job current_job = BqFront(&pool->backend_queue);
            BqPop(&pool->backend_queue);

            pthread_mutex_lock(&pool->working_threads_mutex);
            pool->num_working_threads++;
            pthread_mutex_unlock(&pool->working_threads_mutex);
            pthread_cond_broadcast(&pool->backend_queue_cond);
            pthread_mutex_unlock(&pool->backend_queue_mutex);

            for (int i = current_job.start_id; i < current_job.end_id; ++i) {
                current_job.c[i / pool->matrix_size][i % pool->matrix_size] = 
                    calculation(pool->matrix_size, current_job.a[i / pool->matrix_size], current_job.b[i % pool->matrix_size]);
            }

            pthread_mutex_lock(&pool->backend_queue_mutex);
            pthread_mutex_lock(&pool->working_threads_mutex);
            pool->num_working_threads--;
            pthread_cond_broadcast(&pool->working_threads_cond);
            pthread_mutex_unlock(&pool->working_threads_mutex);
        }
    }
    pthread_exit(NULL);
}

struct tpool *tpool_init(int num_threads, int n) {
    struct tpool *pool = (struct tpool*) calloc(1, sizeof(struct tpool));
    
    pool->num_working_threads = 0;
    pthread_mutex_init(&pool->working_threads_mutex, NULL);
    pthread_cond_init(&pool->working_threads_cond, NULL);

    pthread_mutex_init(&pool->frontend_queue_mutex, NULL);
    pthread_cond_init(&pool->frontend_queue_cond, NULL);
    FqInit(&pool->frontend_queue);

    pthread_mutex_init(&pool->backend_queue_mutex, NULL);
    pthread_cond_init(&pool->backend_queue_cond, NULL);
    BqInit(&pool->backend_queue);

    pool->matrix_size = n;
    pthread_create(&pool->frontend_thread_id, NULL, frontend_job, (void*) pool);
    
    pool->num_threads = num_threads;
    pool->backend_threads_id = (pthread_t*) calloc(num_threads + 1, sizeof(pthread_t));
    for (int i = 0; i < num_threads; ++i) {
        pthread_create(pool->backend_threads_id + i, NULL, backend_job, (void*) pool);
    }

    return pool;
}

void tpool_request(struct tpool *pool, Matrix a, Matrix b, Matrix c,
                   int num_works) {
    pthread_mutex_lock(&pool->frontend_queue_mutex);
    struct frontend_job new_job = {
        .a = a, .b = b, .c = c, 
        .num_works = num_works, 
        .next_job = NULL
    };
    FqPush(&pool->frontend_queue, new_job);
    pthread_mutex_unlock(&pool->frontend_queue_mutex);
    pthread_cond_broadcast(&pool->frontend_queue_cond);
}

void tpool_synchronize(struct tpool *pool) {
    pthread_mutex_lock(&pool->frontend_queue_mutex);
    while (pool->frontend_queue.size > 0) {
        pthread_cond_wait(&pool->frontend_queue_cond, &pool->frontend_queue_mutex);
    }
    pthread_mutex_unlock(&pool->frontend_queue_mutex);

    pthread_mutex_lock(&pool->working_threads_mutex);
    while (pool->num_working_threads > 0) {
        pthread_cond_wait(&pool->working_threads_cond, &pool->working_threads_mutex);
    }
    pthread_mutex_unlock(&pool->working_threads_mutex);

    pthread_mutex_lock(&pool->backend_queue_mutex);
    while (pool->backend_queue.size > 0) {
        pthread_cond_wait(&pool->backend_queue_cond, &pool->backend_queue_mutex);
    }
    pthread_mutex_unlock(&pool->backend_queue_mutex);

    pthread_mutex_lock(&pool->working_threads_mutex);
    while (pool->num_working_threads > 0) {
        pthread_cond_wait(&pool->working_threads_cond, &pool->working_threads_mutex);
    }
    pthread_mutex_unlock(&pool->working_threads_mutex);
}

void tpool_destroy(struct tpool *pool) {
    pool->frontend_queue.size = -1;
    pthread_cond_broadcast(&pool->frontend_queue_cond);
    pthread_join(pool->frontend_thread_id, NULL);
    pool->backend_queue.size = -1;
    pthread_cond_broadcast(&pool->backend_queue_cond);
    
    for (int i = 0; i < pool->num_threads; ++i) {
        pthread_join(pool->backend_threads_id[i], NULL);
    }
    free(pool->backend_threads_id);
    pthread_mutex_destroy(&pool->working_threads_mutex);
    pthread_cond_destroy(&pool->working_threads_cond);
    pthread_mutex_destroy(&pool->frontend_queue_mutex);
    pthread_cond_destroy(&pool->frontend_queue_cond);
    pthread_mutex_destroy(&pool->backend_queue_mutex);
    pthread_cond_destroy(&pool->backend_queue_cond);
    free(pool);
}

void FqInit(struct frontend_queue *fq) {
    fq->head = fq->tail = NULL;
    fq->size = 0;
}

void FqDelete(struct frontend_queue *fq) {
    while (fq->size > 0) {
        FqPop(fq);
    }
}

void FqPush(struct frontend_queue *fq, struct frontend_job new_job) {
    struct frontend_job *queue_job = (struct frontend_job*) calloc(1, sizeof(struct frontend_job));
    *queue_job = new_job;
    fq->size++;
    if (fq->head == NULL) {
        fq->head = fq->tail = queue_job;
    }
    else {
        fq->tail->next_job = queue_job;
        fq->tail = queue_job;
    }
}

void FqPop(struct frontend_queue *fq) {
    fq->size--;
    struct frontend_job *head = fq->head;
    if (head != NULL) {
        fq->head = fq->head->next_job;
    }
    else {
        fq->head = fq->tail = NULL;
    }
    free(head);
}

struct frontend_job FqFront(struct frontend_queue *fq) {
    return *fq->head;
}

void BqInit(struct backend_queue *bq) {
    bq->head = bq->tail = NULL;
    bq->size = 0;
}

void BqDelete(struct backend_queue *bq) {
    while (bq->size > 0) {
        BqPop(bq);
    }
}

void BqPush(struct backend_queue *bq, struct backend_job new_job) {
    struct backend_job *queue_job = (struct backend_job*) calloc(1, sizeof(struct backend_job));
    *queue_job = new_job;
    bq->size++;
    if (bq->head == NULL) {
        bq->head = bq->tail = queue_job;
    }
    else {
        bq->tail->next_job = queue_job;
        bq->tail = queue_job;
    }
}

void BqPop(struct backend_queue *bq) {
    bq->size--;
    struct backend_job *head = bq->head;
    if (head != NULL) {
        bq->head = bq->head->next_job;
    }
    else {
        bq->head = bq->tail = NULL;
    }
    free(head);
}

struct backend_job BqFront(struct backend_queue *bq) {
    return *bq->head;
}