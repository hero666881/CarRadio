#include "log.h"
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>

static void ensure_dir()
{
    struct stat st = {0};
    if (stat("data", &st) == -1)
    {
        mkdir("data", 0755);
    }
}

void write_server_log(char *msg)
{
    ensure_dir();

    FILE *fp = fopen("data/server.log", "a");
    if (!fp)
        return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    fprintf(fp,
        "%04d-%02d-%02d %02d:%02d:%02d [%s]\n",
        t->tm_year + 1900,
        t->tm_mon + 1,
        t->tm_mday,
        t->tm_hour,
        t->tm_min,
        t->tm_sec,
        msg
    );

    fclose(fp);
}
