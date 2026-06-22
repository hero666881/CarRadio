#include "distance.h"
#include <math.h>

static double total_distance = 0;
static double last_lat = 0;
static double last_lng = 0;
static int first_point = 1;

double calc_distance(double lat1, double lng1, double lat2, double lng2)
{
    double R = 6371000;

    double dlat = (lat2 - lat1) * M_PI / 180.0;
    double dlng = (lng2 - lng1) * M_PI / 180.0;

    double a =
        sin(dlat / 2) * sin(dlat / 2) +
        cos(lat1 * M_PI / 180.0) *
        cos(lat2 * M_PI / 180.0) *
        sin(dlng / 2) * sin(dlng / 2);

    double c = 2 * atan2(sqrt(a), sqrt(1 - a));

    return R * c;
}

void update_total_distance(double lat, double lng)
{
    if (first_point)
    {
        last_lat = lat;
        last_lng = lng;
        first_point = 0;
        return;
    }

    double d = calc_distance(last_lat, last_lng, lat, lng);

    if (d > 0.5 && d < 50)
        total_distance += d;

    last_lat = lat;
    last_lng = lng;
}

double get_total_distance()
{
    return total_distance;
}

void reset_distance()
{
    total_distance = 0;
    last_lat = 0;
    last_lng = 0;
    first_point = 1;
}
