#include "threadpool.h"

#include "client.h"

#include <stdio.h>

#include <unistd.h>

#include <stdlib.h>

#include <pthread.h>

#define MAX_THREADS 8

#define MAX_QUEUE 200



typedef struct
{
    int clientfd;

}Task;



typedef struct
{
    pthread_t threads[MAX_THREADS];

    Task queue[MAX_QUEUE];

    int front;

    int rear;

    int count;

    int thread_num;

    int shutdown;

    pthread_mutex_t mutex;

    pthread_cond_t cond;

}ThreadPool;



static ThreadPool pool;



static void* worker(void *arg)
{
    while(1)
    {
        pthread_mutex_lock(

            &pool.mutex

        );



        while(

            pool.count==0

            &&

            !pool.shutdown

        )
        {
            pthread_cond_wait(

                &pool.cond,

                &pool.mutex

            );
        }



        if(pool.shutdown)
        {
            pthread_mutex_unlock(

                &pool.mutex

            );

            pthread_exit(NULL);
        }



        int clientfd;

        clientfd=

        pool.queue[

        pool.front

        ].clientfd;



        pool.front=

        (

        pool.front+1

        )

        %MAX_QUEUE;



        pool.count--;



        pthread_mutex_unlock(

            &pool.mutex

        );



        handle_client(

            clientfd

        );
    }

    return NULL;
}



void threadpool_init(int num)
{
    if(num>MAX_THREADS)
    {
        num=MAX_THREADS;
    }



    pool.front=0;

    pool.rear=0;

    pool.count=0;

    pool.shutdown=0;

    pool.thread_num=num;



    pthread_mutex_init(

        &pool.mutex,

        NULL

    );



    pthread_cond_init(

        &pool.cond,

        NULL

    );



    for(

        int i=0;

        i<num;

        i++

    )
    {
        pthread_create(

            &pool.threads[i],

            NULL,

            worker,

            NULL

        );
    }
}



void threadpool_add_task(
    int clientfd
)
{
    pthread_mutex_lock(

        &pool.mutex

    );



    if(pool.count>=MAX_QUEUE)
    {
        close(clientfd);



        pthread_mutex_unlock(

            &pool.mutex

        );



        return;
    }



    pool.queue[

        pool.rear

    ].clientfd

    = clientfd;



    pool.rear=

    (

    pool.rear+1

    )

    %MAX_QUEUE;



    pool.count++;



    pthread_cond_signal(

        &pool.cond

    );



    pthread_mutex_unlock(

        &pool.mutex

    );
}



void threadpool_destroy()
{
    pthread_mutex_lock(

        &pool.mutex

    );



    pool.shutdown=1;



    pthread_cond_broadcast(

        &pool.cond

    );



    pthread_mutex_unlock(

        &pool.mutex

    );



    for(

        int i=0;

        i<pool.thread_num;

        i++

    )
    {
        pthread_join(

            pool.threads[i],

            NULL

        );
    }



    pthread_mutex_destroy(

        &pool.mutex

    );



    pthread_cond_destroy(

        &pool.cond

    );
}
