#ifndef __PROTO_H__
#define __PROTO_H__

/* ======== TCP 网络配置 ======== */
#define SERVER_IP       "127.0.0.1"     // 本地测试 IP
#define SERVER_PORT     8888            // TCP 服务监听端口

/* ======== 本地 IPC 配置 ======== */
#define CMD_PIPE_PATH   "/tmp/car_cmd"  // UI 控制指令的命名管道路径

/* ======== 系统参数 ======== */
#define MAX_MSG_SIZE    4096            // TCP 接收缓冲区大小

#endif