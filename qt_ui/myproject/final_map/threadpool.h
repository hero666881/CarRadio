#ifndef THREADPOOL_H
#define THREADPOOL_H

void threadpool_init(int num);

void threadpool_add_task(int clientfd);

void threadpool_destroy();

#endif
