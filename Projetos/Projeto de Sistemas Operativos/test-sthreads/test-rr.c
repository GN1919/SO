#include <stdio.h>
#include <sthread.h>

void *worker(void *arg)
{
    int id = (int)(long)arg;
    int i;

    for (i = 0; i < 5; i++) {
        printf("T%d\n", id);
        sthread_yield();
    }

    return NULL;
}

int main()
{
    sthread_init();

    /* todas com a mesma prioridade: comportamento round-robin dentro da fila */
    sthread_create(worker, (void*)1, 5);
    sthread_create(worker, (void*)2, 5);
    sthread_create(worker, (void*)3, 5);

    sthread_sleep(5000);

    return 0;
}
