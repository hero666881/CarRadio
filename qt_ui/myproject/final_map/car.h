#ifndef CAR_H
#define CAR_H

#define MAX_CARS 100

void add_or_update_car(
char *id,
double lat,
double lng
);

void get_all_tracks(
char *json
);

void reset_system();

void remove_offline_car();

#endif
