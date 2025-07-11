#include "async.h"
#include <stdlib.h>
#include <pthread.h>

struct thunk_result {
    async_thunk_id_t id;
    void* result;
    struct thunk_result* next;
};

struct waiting_thunk {
    async_thunk_id_t id;
    async_thunk_t thunk;
    void* args;
    unsigned char awaitable;
    struct waiting_thunk* next;
};

struct async_pool {
    pthread_t* threads;
    int n_threads;
    bool joined;
    bool destroyed;

    struct waiting_thunk* queue_head;
    struct waiting_thunk* queue_tail;
    pthread_cond_t queue_cond;
    async_thunk_id_t next_id;
    pthread_mutex_t queue_lock;

    pthread_cond_t result_cond;
    struct thunk_result** thunk_results;
    int result_buckets;
    pthread_mutex_t result_lock;
};

static void store_result(async_pool_t* pool, async_thunk_id_t thunk_id, void* result) {
    struct thunk_result* stored_result = malloc(sizeof(struct thunk_result));
    if (stored_result == NULL) return;
    stored_result->id = thunk_id;
    stored_result->result = result;
    stored_result->next = NULL;

    unsigned int bucket = thunk_id % pool->result_buckets;
    struct thunk_result* node = pool->thunk_results[bucket];
    if (node == NULL) {
        pool->thunk_results[bucket] = stored_result;
        return;
    }

    while (node->next != NULL) node = node->next;
    node->next = stored_result;
}

static void* thread_loop(void* args) {
    async_pool_t* pool = (async_pool_t*) args;
    pthread_mutex_lock(&pool->queue_lock);
    while (!pool->destroyed) {
        if (pool->queue_head == NULL) {
            if (pool->joined) break;
            pthread_cond_wait(&pool->queue_cond, &pool->queue_lock);
            continue;
        }

        struct waiting_thunk* queue_item = pool->queue_head;
        async_thunk_id_t thunk_id = queue_item->id;
        async_thunk_t thunk = queue_item->thunk;
        void* args = queue_item->args;
        if (queue_item->next == NULL) {
            pool->queue_head = NULL;
            pool->queue_tail = NULL;
        } else {
            pool->queue_head = queue_item->next;
        }
        pthread_mutex_unlock(&pool->queue_lock);

        bool awaitable = queue_item->awaitable;
        free(queue_item);
        void* result = thunk(args);

        pthread_mutex_lock(&pool->result_lock);
        if (awaitable) store_result(pool, thunk_id, result);
        pthread_mutex_unlock(&pool->result_lock);
        pthread_cond_broadcast(&pool->result_cond);

        pthread_mutex_lock(&pool->queue_lock);
    }
    pthread_mutex_unlock(&pool->queue_lock);
    return NULL;
}

static void abort_pool(async_pool_t* pool) {
    pthread_mutex_destroy(&pool->queue_lock);
    pthread_cond_destroy(&pool->queue_cond);
    free(pool->threads);
    free(pool);
}

async_pool_t* async_pool_create(int n_threads) {
    if (n_threads < 1) return NULL;
    async_pool_t* pool = malloc(sizeof(async_pool_t));
    if (pool == NULL) return NULL;

    pool->threads = malloc(sizeof(pthread_t) * n_threads);
    if (pool->threads == NULL) {
        free(pool);
        return NULL;
    }

    pool->result_buckets = n_threads;
    pool->thunk_results = calloc(pool->result_buckets, sizeof(struct thunk_result*));
    if (pool->thunk_results == NULL) {
        free(pool->threads);
        free(pool);
        return NULL;
    }

    pthread_cond_init(&pool->queue_cond, NULL);
    pthread_cond_init(&pool->result_cond, NULL);
    pthread_mutex_init(&pool->result_lock, NULL);
    pthread_mutex_init(&pool->queue_lock, NULL);
    if (pthread_mutex_lock(&pool->queue_lock) != 0) {
        abort_pool(pool);
        return NULL;
    }

    for (pool->n_threads = 0; pool->n_threads < n_threads; pool->n_threads++) {
        if (pthread_create(&pool->threads[pool->n_threads], NULL, thread_loop, pool) != 0)
            break;
    }

    pool->next_id = 1;
    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->joined = false;
    pool->destroyed = false;
    if (pool->n_threads < 1 || pthread_mutex_unlock(&pool->queue_lock) != 0) {
        pool->destroyed = true;
        int i;
        for (i = 0; i < pool->n_threads; i++) {
            pthread_cancel(pool->threads[i]);
        }
        abort_pool(pool);
        return NULL;
    };
    return pool;
}

static void destroy_queue(struct waiting_thunk* head) {
    if (head == NULL) return;
    destroy_queue(head->next);
    free(head);
}

static void destroy_result(struct thunk_result* head) {
    if (head == NULL) return;
    destroy_result(head->next);
    free(head);
}

async_thunk_id_t async_submit(async_pool_t* pool, async_thunk_t thunk, void* args, bool awaitable) {
    struct waiting_thunk* item = malloc(sizeof(struct waiting_thunk));
    if (item == NULL) return ASYNC_NO_THUNK;
    item->thunk = thunk;
    item->args = args;
    item->awaitable = awaitable;
    item->next = NULL;

    pthread_mutex_lock(&pool->queue_lock);
    if (pool->joined || pool->destroyed) {
        pthread_mutex_unlock(&pool->queue_lock);
        free(item);
        return ASYNC_NO_THUNK;
    }

    item->id = pool->next_id++;
    if (pool->queue_head == NULL) {
        pool->queue_head = item;
        pool->queue_tail = item;
    } else {
        pool->queue_tail->next = item;
        pool->queue_tail = item;
    }
    pthread_mutex_unlock(&pool->queue_lock);
    pthread_cond_signal(&pool->queue_cond);
    return item->id;
}

static struct thunk_result* pop_result(async_pool_t* pool, async_thunk_id_t thunk_id) {
    unsigned int bucket = thunk_id % pool->result_buckets;
    struct thunk_result* node = pool->thunk_results[bucket];
    if (node == NULL) return NULL;
    if (node->id == thunk_id) {
        pool->thunk_results[bucket] = node->next;
        return node;
    }
    while (node->next != NULL) {
        struct thunk_result* candidate = node->next;
        if (candidate->id == thunk_id) {
            node->next = candidate->next;
            return candidate;
        }
        node = candidate;
    }
    return NULL;
}

void* async_await(async_pool_t* pool, async_thunk_id_t thunk_id) {
    pthread_mutex_lock(&pool->result_lock);
    while (!pool->destroyed) {
        struct thunk_result* found = pop_result(pool, thunk_id);
        if (found != NULL) {
            pthread_mutex_unlock(&pool->result_lock);
            void* result = found->result;
            free(found);
            return result;
        }

        pthread_cond_wait(&pool->result_cond, &pool->result_lock);
    }
    pthread_mutex_unlock(&pool->result_lock);
    return NULL;
}

void async_pool_join(async_pool_t* pool) {
    pthread_mutex_lock(&pool->queue_lock);
    if (pool->joined) {
        pthread_mutex_unlock(&pool->queue_lock);
        return;
    }
    pool->joined = true;
    pthread_cond_broadcast(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_lock);

    int i;
    for (i = 0; i < pool->n_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    free(pool->threads);

    pthread_cond_destroy(&pool->queue_cond);
    destroy_queue(pool->queue_head);
}

void async_pool_destroy(async_pool_t* pool) {
    async_pool_join(pool);

    // the pattern lock / flag forbidden / broadcast / unlock / relock
    // should place us last in the order of threads waiting for a lock,
    // so we can safely destroy it
    pthread_mutex_lock(&pool->queue_lock);
    pthread_mutex_destroy(&pool->queue_lock);

    pthread_mutex_lock(&pool->result_lock);
    pool->destroyed = true;
    pthread_cond_broadcast(&pool->result_cond);
    pthread_mutex_unlock(&pool->result_lock);
    pthread_mutex_lock(&pool->result_lock);
    pthread_mutex_destroy(&pool->result_lock);
    pthread_cond_destroy(&pool->result_cond);

    int i;
    for (i = 0; i < pool->result_buckets; i++) {
        destroy_result(pool->thunk_results[i]);
    }
    free(pool->thunk_results);

    free(pool);
}
