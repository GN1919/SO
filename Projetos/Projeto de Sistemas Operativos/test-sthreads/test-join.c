#include <stdio.h>
#include <sthread.h>

sthread_t tid;

void *child(void *arg)
{
    printf("child running\n");
    sthread_sleep(500);
    printf("child exiting\n");
    sthread_exit((void*)1234);
    return NULL;
}

void *parent(void *arg)
{
    void *ret;
    printf("parent waiting\n");
    sthread_join(tid, &ret);
    printf("joined value=%ld\n", (long)ret);
    return NULL;
}

int main()
{
    sthread_init();

    tid = sthread_create(child, NULL, 5);
    sthread_create(parent, NULL, 5);

    sthread_exit(NULL);
    return 0;
}
