#ifndef GPS_H

#define GPS_H

void save_gps(

char *carid,

double lat,

double lng

);

void get_track_by_id(

char *carid,

char *json

);

#endif

