#ifndef DISTANCE_H
#define DISTANCE_H

double calc_distance(
    double lat1,
    double lng1,
    double lat2,
    double lng2
);

void update_total_distance(double lat, double lng);
double get_total_distance();
void reset_distance();

#endif
