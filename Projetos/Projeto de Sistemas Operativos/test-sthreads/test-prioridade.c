#include <stdio.h>
#include <sthread.h>

void *low(void *arg)
{
    for (int i = 0; i < 5; i++) {
        printf("LOW\n");
        sthread_yield();
    }

    return NULL;
}

void *high(void *arg)
{
    for (int i = 0; i < 5; i++) {
        printf("HIGH\n");
        sthread_yield();
    }

    return NULL;
}

int main()
{
    sthread_init();

    /* high tem prioridade 5 (valor mais baixo = mais prioritária) */
    sthread_create(high, NULL, 5);

    /* low tem prioridade 10 (valor mais alto = menos prioritária) */
    sthread_create(low, NULL, 10);

    sthread_sleep(2000);

    return 0;
}
