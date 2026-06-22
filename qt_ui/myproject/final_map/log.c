#include "log.h"

#include <stdio.h>

#include <time.h>

#include <pthread.h>



static pthread_mutex_t log_mutex=

PTHREAD_MUTEX_INITIALIZER;



void write_server_log(
    char *msg
)
{
    pthread_mutex_lock(

        &log_mutex

    );



    FILE *fp;

    fp=fopen(

        "data/server.log",

        "a"

    );



    if(fp)
    {
        time_t now=time(NULL);

        struct tm *tm=localtime(&now);

        char time_str[64];

        strftime(

            time_str,

            sizeof(time_str),

            "%Y-%m-%d %H:%M:%S",

            tm

        );



        fprintf(

            fp,

            "%s %s\n",

            time_str,

            msg

        );



        fclose(fp);
    }



    pthread_mutex_unlock(

        &log_mutex

    );
}
