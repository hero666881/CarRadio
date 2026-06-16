#include "server.h"
#include "gps.h"
#include "log.h"
#include "distance.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

void start_server()
{
    int serverfd;
    struct sockaddr_in server;

    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0)
    {
        perror("socket");
        return;
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    server.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(serverfd, (struct sockaddr*)&server, sizeof(server)) < 0)
    {
        perror("bind");
        return;
    }

    if (listen(serverfd, 10) < 0)
    {
        perror("listen");
        return;
    }

    printf("服务器启动成功\n");
    write_server_log("服务器启动");

    while (1)
    {
        int clientfd = accept(serverfd, NULL, NULL);
        if (clientfd < 0) continue;

        char buf[4096] = {0};
        int len = recv(clientfd, buf, sizeof(buf) - 1, 0);
        if (len <= 0)
        {
            close(clientfd);
            continue;
        }

        buf[len] = '\0';

        /* ================= GPS ================= */
        if (strstr(buf, "POST /gps"))
        {
            char *body = strstr(buf, "\r\n\r\n");

            if (body)
            {
                body += 4;

                double lat, lng;

                if (sscanf(body, "%lf %lf", &lat, &lng) == 2)
                {
                    save_gps(lat, lng);
                    update_total_distance(lat, lng);

                    printf("GPS: %.6lf %.6lf\n", lat, lng);
                    printf("Total: %.2lf m\n", get_total_distance());
                }
            }

            char response[] =
                "HTTP/1.1 200 OK\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n";

            send(clientfd, response, strlen(response), 0);
        }

        /* ================= TRACKS（关键修复） ================= */
        else if (strstr(buf, "GET /tracks"))
        {
            char json[65536] = {0};
            get_tracks(json);

            char response[70000];

            snprintf(response, sizeof(response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "{\"distance\":%.2f,\"points\":%s}",
                get_total_distance(),
                json
            );

            send(clientfd, response, strlen(response), 0);
        }

        /* ================= RESET ================= */
        else if (strstr(buf, "GET /reset"))
        {
            FILE *fp = fopen("data/gps.log", "w");
            if (fp) fclose(fp);

            reset_distance();

            printf("系统已复位\n");
            write_server_log("系统复位");

            char response[] =
                "HTTP/1.1 200 OK\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n";

            send(clientfd, response, strlen(response), 0);
        }

        close(clientfd);
    }

    close(serverfd);
}
