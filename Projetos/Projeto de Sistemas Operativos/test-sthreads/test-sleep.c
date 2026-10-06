#include <stdio.h>
#include <sthread.h>

void *sleeper(void *arg)
{
    printf("sleep begin\n");
    sthread_sleep(1000);
    printf("sleep end\n");
    return NULL;
}

void *worker(void *arg)
{
    int i;
    for (i = 0; i < 20; i++) {
        printf("working %d\n", i);
        sthread_yield();
    }
    return NULL;
}

int main()
{
    sthread_init();

    sthread_create(sleeper, NULL, 5);
    sthread_create(worker, NULL, 5);

    sthread_exit(NULL);

    return 0;
}
