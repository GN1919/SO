#include <stdio.h>
#include <sthread.h>

void *t1(void *arg)
{
    while(1) {
        printf("A ");
        sthread_yield();
    }
}

void *t2(void *arg)
{
    while(1) {
        printf("B ");
        sthread_yield();
    }
}

void *t3(void *arg)
{
    while(1) {
        printf("C ");
        sthread_yield();
    }
}

int main()
{
    sthread_init();

    sthread_create(t1, NULL, 5);
    sthread_create(t2, NULL, 5);
    sthread_create(t3, NULL, 5);

    while(1)
        sthread_yield();
}