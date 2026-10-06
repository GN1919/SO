#include <stdio.h>
#include <sthread.h>

sthread_mutex_t mutex;

int counter = 0;

void *worker(void *arg)
{
    int i;

    for (i = 0; i < 1000; i++) {

        sthread_mutex_lock(mutex);

        counter++;

        sthread_mutex_unlock(mutex);
    }

    return NULL;
}

int main()
{
    sthread_init();

    mutex = sthread_mutex_init();

    /* 3 workers com a mesma prioridade dinâmica */
    sthread_create(worker, NULL, 5);
    sthread_create(worker, NULL, 5);
    sthread_create(worker, NULL, 5);

    /* a main thread (prioridade 5) fica a executar até as workers acabarem */
    /* num cenário real usaria sthread_join; aqui o exit() das workers termina */

    printf("counter=%d\n", counter);

    return 0;
}
