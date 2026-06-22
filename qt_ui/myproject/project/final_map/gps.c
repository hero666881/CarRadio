#include "gps.h"

#include <stdio.h>

#include <string.h>

#include <ctype.h>

#include <time.h>

#include <pthread.h>

#include <sys/stat.h>



static pthread_mutex_t gps_mutex=

PTHREAD_MUTEX_INITIALIZER;



static void create_dir()
{
    struct stat st={0};

    if(
        stat(
            "data",
            &st
        )==-1
    )
    {
        mkdir(
            "data",
            0755
        );
    }
}



static void sanitize_carid(
    char *out,

    const char *in
)
{
    while(*in)
    {
        if(isalnum(*in)||*in=='_'||*in=='-')
        {
            *out++=*in;
        }

        in++;
    }

    *out='\0';
}



void save_gps(
    char *carid,

    double lat,

    double lng
)
{

    pthread_mutex_lock(

        &gps_mutex

    );



    create_dir();



    char safe_id[32];

    sanitize_carid(

        safe_id,

        carid

    );



    char filename[128];



    snprintf(

        filename,

        sizeof(filename),

        "data/gps_%s.log",

        safe_id

    );



    FILE *fp;

    fp=fopen(

        filename,

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

                "%.6lf %.6lf %s\n",

                lat,

                lng,

                time_str

            );



        fclose(fp);
    }



    pthread_mutex_unlock(

        &gps_mutex

    );
}



void get_track_by_id(
    char *carid,

    char *json
)
{
    pthread_mutex_lock(

        &gps_mutex

    );



    char safe_id[32];

    sanitize_carid(

        safe_id,

        carid

    );



    char filename[128];



    snprintf(

        filename,

        sizeof(filename),

        "data/gps_%s.log",

        safe_id

    );



    FILE *fp;

    fp=fopen(

        filename,

        "r"

    );



    if(!fp)
    {
        strcpy(

            json,

            "[]"

        );



        pthread_mutex_unlock(

            &gps_mutex

        );



        return;
    }



    size_t offset=0;

    int first=1;



    offset += sprintf(

        json+offset,

        "["

    );



    char line[512];



    while(

        fgets(

            line,

            sizeof(line),

            fp

        )

    )
    {
        double lat;

        double lng;



        if(

            sscanf(

                line,

                "%lf %lf",

                &lat,

                &lng

            )!=2

        )
        {
            continue;
        }



        if(!first)
        {
            offset += sprintf(

                json+offset,

                ","

            );
        }



        offset += sprintf(

            json+offset,

            "{\"lat\":%.6lf,\"lng\":%.6lf}",

            lat,

            lng

        );



        first=0;
    }



    sprintf(

        json+offset,

        "]"

    );



    fclose(fp);



    pthread_mutex_unlock(

        &gps_mutex

    );
}


