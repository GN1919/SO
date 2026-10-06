#include <stdio.h>
#include <sthread.h>

sthread_mon_t mon;
int ready = 0;

void *consumer(void *arg)
{
    sthread_monitor_enter(mon);
    while (!ready) {
        printf("consumer waiting\n");
        sthread_monitor_wait(mon);
    }
    printf("consumer resumed\n");
    sthread_monitor_exit(mon);
    return NULL;
}

void *producer(void *arg)
{
    sthread_sleep(500);
    sthread_monitor_enter(mon);
    ready = 1;
    printf("producer signaling\n");
    sthread_monitor_signal(mon);
    sthread_monitor_exit(mon);
    return NULL;
}

int main()
{
    sthread_init();

    mon = sthread_monitor_init();

    sthread_create(consumer, NULL, 5);
    sthread_create(producer, NULL, 5);

    sthread_exit(NULL);
    return 0;
}
