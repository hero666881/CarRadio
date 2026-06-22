#include "car.h"

#include "gps.h"

#include "distance.h"

#include <stdio.h>

#include <string.h>

#include <time.h>

#include <pthread.h>

#include <dirent.h>

#include <stdlib.h>

typedef struct
{
char id[32];

double lat;

double lng;

time_t update_time;

}Car;

static Car cars[MAX_CARS];

static int car_count=0;

static pthread_mutex_t car_mutex=

PTHREAD_MUTEX_INITIALIZER;

static int find_car(char *id)
{
for(int i=0;i<car_count;i++)
{
if(strcmp(cars[i].id,id)==0)
{
return i;
}
}

return -1;
}

void add_or_update_car(
char *id,

double lat,

double lng
)
{
pthread_mutex_lock(

&car_mutex

);

int index=find_car(id);

if(index==-1)
{
if(car_count<MAX_CARS)
{
strcpy(

cars[car_count].id,

id

);

cars[car_count].lat=lat;

cars[car_count].lng=lng;

cars[car_count].update_time=

time(NULL);

car_count++;
}
}
else
{
cars[index].lat=lat;

cars[index].lng=lng;

cars[index].update_time=

time(NULL);
}

pthread_mutex_unlock(

&car_mutex

);
}



void remove_offline_car()
{
time_t now=time(NULL);

for(int i=0;i<car_count;)
{
if(now-cars[i].update_time>30)
{
for(int j=i;j<car_count-1;j++)
{
cars[j]=cars[j+1];
}

car_count--;

continue;
}

i++;
}
}



void get_all_tracks(char *json)
{
pthread_mutex_lock(

&car_mutex

);

remove_offline_car();

size_t offset=0;

offset+=sprintf(

json+offset,

"{\"cars\":["

);

for(int i=0;i<car_count;i++)
{
char *track=

malloc(65536);

memset(

track,

0,

65536

);

get_track_by_id(

cars[i].id,

track

);

if(i)
{
offset+=sprintf(

json+offset,

","
);
}

offset+=sprintf(

json+offset,

"{"

"\"id\":\"%s\","

"\"lat\":%.6lf,"

"\"lng\":%.6lf,"

"\"distance\":%.2lf,"

"\"points\":%s"

"}",

cars[i].id,

cars[i].lat,

cars[i].lng,

get_distance(

cars[i].id

),

track

);

free(track);
}

sprintf(

json+offset,

"]}"

);

pthread_mutex_unlock(

&car_mutex

);
}



void reset_system()
{
pthread_mutex_lock(

&car_mutex

);

DIR *dir;

struct dirent *entry;

dir=opendir(

"data"

);

if(dir)
{
while(

(entry=readdir(dir))

!=NULL

)
{
if(strstr(

entry->d_name,

"gps_"

))
{
char file[300];

snprintf(

file,

sizeof(file),

"data/%s",

entry->d_name

);

remove(file);
}
}

closedir(dir);
}

car_count=0;

reset_all_distance();

pthread_mutex_unlock(

&car_mutex

);
}
