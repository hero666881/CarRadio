#include "distance.h"

#include <string.h>

#include <math.h>

#include <pthread.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_CARS 100



typedef struct
{
    char id[32];

    double total;

    double last_lat;

    double last_lng;

}Distance;



static Distance dis[MAX_CARS];

static int count=0;



static pthread_mutex_t dis_mutex=

PTHREAD_MUTEX_INITIALIZER;



static int find_car(
    char *id
)
{
    for(
        int i=0;

        i<count;

        i++
    )
    {
        if(
            strcmp(
                dis[i].id,
                id
            )==0
        )
        {
            return i;
        }
    }

    return -1;
}



static double calc(
    double lat1,

    double lng1,

    double lat2,

    double lng2
)
{
    double R=6371000;

    double dlat=

    (lat2-lat1)

    *M_PI/180;



    double dlng=

    (lng2-lng1)

    *M_PI/180;



    double a=

    sin(dlat/2)

    *sin(dlat/2)

    +

    cos(lat1*M_PI/180)

    *

    cos(lat2*M_PI/180)

    *

    sin(dlng/2)

    *

    sin(dlng/2);



    double c=

    2*

    atan2(

        sqrt(a),

        sqrt(1-a)

    );



    return R*c;
}



void update_distance(
    char *id,

    double lat,

    double lng
)
{
    pthread_mutex_lock(

        &dis_mutex

    );



    int index;

    index=find_car(id);



    if(index==-1)
    {
        strcpy(

            dis[count].id,

            id

        );



        dis[count].total=0;



        dis[count].last_lat=lat;



        dis[count].last_lng=lng;



        count++;



        pthread_mutex_unlock(

            &dis_mutex

        );



        return;
    }



    double d;

    d=calc(

        dis[index].last_lat,

        dis[index].last_lng,

        lat,

        lng

    );



    if(d>0.5)
    {
        dis[index].total+=d;
    }



    dis[index].last_lat=lat;

    dis[index].last_lng=lng;



    pthread_mutex_unlock(

        &dis_mutex

    );
}



double get_distance(
    char *id
)
{
    pthread_mutex_lock(

        &dis_mutex

    );



    int index;

    index=find_car(id);



    if(index==-1)
    {
        pthread_mutex_unlock(

            &dis_mutex

        );



        return 0;
    }



    double d=

    dis[index].total;



    pthread_mutex_unlock(

        &dis_mutex

    );



    return d;
}



void reset_all_distance()
{
    pthread_mutex_lock(

        &dis_mutex

    );



    count=0;



    pthread_mutex_unlock(

        &dis_mutex

    );
}
