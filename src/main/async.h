#ifndef ASYNC_H
#define ASYNC_H 1
#include <stdbool.h>

#define ASYNC_NO_THUNK 0

struct async_pool;
typedef struct async_pool async_pool_t;
typedef void*(*async_thunk_t)(void*);
typedef unsigned int async_thunk_id_t;

async_pool_t* async_pool_create(int n_threads);
async_thunk_id_t async_submit(async_pool_t* pool, async_thunk_t thunk, void* args, bool awaitable);
void* async_await(async_pool_t* pool, async_thunk_id_t thunk_id);
void async_pool_join(async_pool_t* pool);
void async_pool_destroy(async_pool_t* pool);

#endif
