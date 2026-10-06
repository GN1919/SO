#include <stdio.h>
#include <sthread.h>

sthread_mutex_t m1;
sthread_mutex_t m2;

void *t1(void *arg)
{
    sthread_mutex_lock(m1);
    printf("t1 adquiriu m1\n");
    sthread_sleep(100);
    printf("t1 tenta adquirir m2...\n");
    sthread_mutex_lock(m2);
    sthread_mutex_unlock(m2);
    sthread_mutex_unlock(m1);
    return NULL;
}

void *t2(void *arg)
{
    sthread_mutex_lock(m2);
    printf("t2 adquiriu m2\n");
    sthread_sleep(100);
    printf("t2 tenta adquirir m1...\n");
    sthread_mutex_lock(m1);
    sthread_mutex_unlock(m1);
    sthread_mutex_unlock(m2);
    return NULL;
}

int main()
{
    sthread_init();

    m1 = sthread_mutex_init();
    m2 = sthread_mutex_init();

    sthread_create(t1, NULL, 5);
    sthread_create(t2, NULL, 5);

    sthread_exit(NULL);

    return 0;
}
