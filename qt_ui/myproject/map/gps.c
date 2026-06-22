#include "gps.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <stdlib.h>

static void ensure_dir()
{
    struct stat st = {0};
    if (stat("data", &st) == -1)
    {
        mkdir("data", 0755);
    }
}

void save_gps(double lat, double lng)
{
    ensure_dir();

    FILE *fp = fopen("data/gps.log", "a");
    if (!fp)
        return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    fprintf(fp,
        "%04d-%02d-%02d %02d:%02d:%02d %.6lf %.6lf\n",
        t->tm_year + 1900,
        t->tm_mon + 1,
        t->tm_mday,
        t->tm_hour,
        t->tm_min,
        t->tm_sec,
        lat,
        lng
    );

    fclose(fp);
}

void get_tracks(char *json)
{
    FILE *fp = fopen("data/gps.log", "r");

    if (!fp)
    {
        strcpy(json, "[]");
        return;
    }

    char line[256];
    int first = 1;
    size_t offset = 0;

    offset += snprintf(json + offset, 65536 - offset, "[");

    while (fgets(line, sizeof(line), fp))
    {
        double lat, lng;

        if (sscanf(line, "%*s %*s %lf %lf", &lat, &lng) != 2)
            continue;

        if (first)
        {
            offset += snprintf(json + offset, 65536 - offset,
                "{\"lat\":%.6lf,\"lng\":%.6lf}", lat, lng);
            first = 0;
        }
        else
        {
            offset += snprintf(json + offset, 65536 - offset,
                ",{\"lat\":%.6lf,\"lng\":%.6lf}", lat, lng);
        }

        if (offset > 65000) break; // 防爆
    }

    snprintf(json + offset, 65536 - offset, "]");

    fclose(fp);
}
