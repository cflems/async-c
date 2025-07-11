#include "async.h"
#include <stdio.h>
#include <unistd.h>

static int func2(int a) {
    sleep(a);
    return 2*a;
}

/*
static void func1(int a) {
    printf("func1: %d\n", func2(a));
}

static void func3() {
    printf("func3!\n");
}
*/

int main() {
    async_pool_t* pool = async_pool_create(2);
    async_thunk_id_t id1 = async_submit(pool, func2, 7, true);
    async_thunk_id_t id2 = async_submit(pool, func2, 3, true);
    async_thunk_id_t id3 = async_submit(pool, func2, 1, true);
    async_thunk_id_t id4 = async_submit(pool, func2, 5, true);
    async_pool_join(pool);
    printf("3: %d\n", (int) async_await(pool, id3));
    printf("4: %d\n", (int) async_await(pool, id4));
    printf("1: %d\n", (int) async_await(pool, id1));
    printf("2: %d\n", (int) async_await(pool, id2));
    async_pool_destroy(pool);
    return 0;
}
