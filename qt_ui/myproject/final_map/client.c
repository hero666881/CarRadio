#include "client.h"

#include "car.h"

#include "gps.h"

#include "distance.h"

#include "log.h"

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <unistd.h>

#include <sys/socket.h>

void handle_client(int clientfd)
{
    char buf[8192]={0};

    int len;

    len=recv(

        clientfd,

        buf,

        sizeof(buf)-1,

        0

    );

    if(len<=0)
    {
        close(clientfd);

        return;
    }

    buf[len]='\0';



    if(strstr(buf,"POST /gps"))
    {
        char *body;

        body=strstr(

            buf,

            "\r\n\r\n"

        );



        if(body)
        {
            body+=4;



            char carid[32];

            double lat;

            double lng;



            if(

            sscanf(

            body,

            "%31s %lf %lf",

            carid,

            &lat,

            &lng

            )==3

            )
            {
                printf(

                    "=================\n"

                );

                printf(

                    "[GPS上传]\n"

                );

                printf(

                    "车辆:%s\n",

                    carid

                );

                printf(

                    "纬度:%.6lf\n",

                    lat

                );

                printf(

                    "经度:%.6lf\n",

                    lng

                );

                printf(

                    "=================\n"

                );



                fflush(stdout);



                add_or_update_car(

                    carid,

                    lat,

                    lng

                );



                save_gps(

                    carid,

                    lat,

                    lng

                );



                update_distance(

                    carid,

                    lat,

                    lng

                );



                char log_msg[256];

                snprintf(

                    log_msg,

                    sizeof(log_msg),

                    "%s %.6f %.6f",

                    carid,

                    lat,

                    lng

                );



                write_server_log(

                    log_msg

                );
            }
        }



        char response[] =

        "HTTP/1.1 200 OK\r\n"

        "Access-Control-Allow-Origin: *\r\n"

        "\r\n"

        "ok";



        send(

            clientfd,

            response,

            strlen(response),

            0

        );
    }



    else if(strstr(buf,"GET /tracks"))
    {
        char *json=

        malloc(262144);



        memset(

            json,

            0,

            262144

        );



        get_all_tracks(

            json

        );



        char *header=

        malloc(270000);



        snprintf(

            header,

            270000,

            "HTTP/1.1 200 OK\r\n"

            "Content-Type: application/json\r\n"

            "Access-Control-Allow-Origin: *\r\n"

            "\r\n"

            "%s",

            json

        );



        send(

            clientfd,

            header,

            strlen(header),

            0

        );



        free(json);

        free(header);
    }



    else if(strstr(buf,"GET /reset"))
    {
        reset_system();



        char response[] =

        "HTTP/1.1 200 OK\r\n"

        "Access-Control-Allow-Origin: *\r\n"

        "\r\n"

        "reset";



        send(

            clientfd,

            response,

            strlen(response),

            0

        );
    }



    close(clientfd);
}
