#include <stdio.h>

#include "platform.h"

#if defined PLATFORM_WINDOWS
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "threadpool.h"
#include "mutex.h"
#include "thread.h"

static struct mutex_s* count_mutex = 0;
static int counter = 0;

static void showcounter()
{
    #if defined PLATFORM_WINDOWS
printf("############### ThreadID : %d\n", GetCurrentThreadId());
#else
printf("############### ThreadID : %d\n", (int)pthread_self());
#endif

    
    printf("wait counter lock\n");
    mutex_lock(count_mutex);
    counter++;

    printf("current count : %d\n", counter);
    mutex_unlock(count_mutex);

    printf("un counter lock\n");
}

static void my_msg_fun(struct thread_pool_s* self, void* msg)
{
    thread_sleep(2000);
    showcounter();
}

int main()
{
    int i= 0;
    struct thread_pool_s* p = thread_pool_new(my_msg_fun, 4, 4000);
    count_mutex = mutex_new();
    thread_pool_start(p);
    
    thread_sleep(1000);
    thread_pool_pushmsg(p, (void*)3);
    thread_pool_pushmsg(p, (void*)3);
    thread_pool_pushmsg(p, (void*)3);

    thread_pool_pushmsg(p, (void*)3);
    thread_pool_pushmsg(p, (void*)3);
    thread_pool_pushmsg(p, (void*)3);
    thread_pool_pushmsg(p, (void*)3);
    thread_pool_pushmsg(p, (void*)3);
    thread_pool_pushmsg(p, (void*)3);
    thread_pool_pushmsg(p, (void*)3);
    thread_pool_pushmsg(p, (void*)3);

    thread_pool_wait(p);

    printf("�̳߳ع������\n");

    thread_pool_wait(p);

    printf("�̳߳ع������\n");

    thread_pool_pushmsg(p, (void*)3);
    thread_pool_wait(p);

    printf("�̳߳ع������\n");
    getchar();

    while(1)
    {
        if(i < 3001)
        {
            //thread_pool_pushmsg(p, (void*)3);
            i++;
        }
        else
        {
            //break;
        }
    }

    thread_pool_delete(p);
    return 0;
}
