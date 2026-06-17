/** * @file proto.h 
 * @brief CarRadio 车载收音机共享通信协议常量定义
 * * 本文件包含了客户端与服务端共用的基础配置：
 * - TCP 网络通信的 IP 地址与端口号
 * - 进程间通信 (IPC) 的命名管道 (FIFO) 路径
 * - 用于数据传输与控制命令的缓冲区大小 (Buffer Sizes)
 */
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