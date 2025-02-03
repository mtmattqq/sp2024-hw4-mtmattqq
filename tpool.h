#pragma once

#include <pthread.h>

/* You may define additional structures / typedef's in this header file if
 * needed.
 */

typedef int** Matrix;
typedef int* Vector;

struct frontend_job {
    Matrix a, b, c;
    int num_works;
    struct frontend_job *next_job;
};

struct backend_job {
    Matrix a, b, c;
    int start_id, end_id;
    struct backend_job *next_job;
};

struct frontend_queue {
    struct frontend_job *head, *tail;
    int size;
};

void FqInit(struct frontend_queue *fq);
void FqDelete(struct frontend_queue *fq);
void FqPush(struct frontend_queue *fq, struct frontend_job new_job);
void FqPop(struct frontend_queue *fq);
struct frontend_job FqFront(struct frontend_queue *fq);

struct backend_queue {
    struct backend_job *head, *tail;
    int size;
};

void BqInit(struct backend_queue *bq);
void BqDelete(struct backend_queue *bq);
void BqPush(struct backend_queue *bq, struct backend_job new_job);
void BqPop(struct backend_queue *bq);
struct backend_job BqFront(struct backend_queue *bq);

struct tpool {
    int matrix_size;
    pthread_t frontend_thread_id;
    
    int num_threads;
    pthread_t *backend_threads_id;

    int num_working_threads; // including frontend and backend
    pthread_mutex_t working_threads_mutex;
    pthread_cond_t working_threads_cond;

    pthread_mutex_t frontend_queue_mutex;
    pthread_cond_t frontend_queue_cond;
    struct frontend_queue frontend_queue;

    pthread_mutex_t backend_queue_mutex;
    pthread_cond_t backend_queue_cond;
    struct backend_queue backend_queue;
};

struct tpool* tpool_init(int num_threads, int n);
void tpool_request(struct tpool*, Matrix a, Matrix b, Matrix c, int num_works);
void tpool_synchronize(struct tpool*);
void tpool_destroy(struct tpool*);
int calculation(int n, Vector, Vector);  // Already implemented
