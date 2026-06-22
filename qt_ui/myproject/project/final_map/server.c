#include "server.h"

#include "threadpool.h"

#include <stdio.h>

#include <string.h>

#include <unistd.h>

#include <arpa/inet.h>

#include <sys/socket.h>

#define PORT 8080

#define THREAD_NUM 4

void start_server()
{
    int serverfd;

    int clientfd;

    int opt = 1;

    struct sockaddr_in server;

    serverfd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if(serverfd < 0)
    {
        perror("socket");

        return;
    }

    setsockopt(
        serverfd,

        SOL_SOCKET,

        SO_REUSEADDR,

        &opt,

        sizeof(opt)
    );

    memset(
        &server,
        0,
        sizeof(server)
    );

    server.sin_family = AF_INET;

    server.sin_port = htons(PORT);

    server.sin_addr.s_addr = INADDR_ANY;

    if(bind(
        serverfd,

        (struct sockaddr*)&server,

        sizeof(server)

    ) < 0)
    {
        perror("bind");

        return;
    }

    if(listen(
        serverfd,

        100

    ) < 0)
    {
        perror("listen");

        return;
    }

    threadpool_init(
        THREAD_NUM
    );

    printf("运行中\n");

    fflush(stdout);

    while(1)
    {
        clientfd = accept(

            serverfd,

            NULL,

            NULL

        );

        if(clientfd < 0)
        {
            continue;
        }

        threadpool_add_task(
            clientfd
        );
    }

    close(serverfd);
}
