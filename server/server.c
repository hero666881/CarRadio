#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <signal.h>
#include "proto.h"

// ========== 动态歌曲路由表 ==========
char  **song_table = NULL;
int     song_count = 0;

// 信号处理：忽略 SIGPIPE，防止客户端断开导致服务端退出
static void on_signal(int sig) {
    if (sig == SIGPIPE) {
        // 忽略 SIGPIPE
        return;
    }
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\n[系统提示] 收到退出信号，服务端关闭...\n");
        exit(0);
    }
}

// ----- 扫描子目录，统计 .mp3 文件数 -----
static int count_mp3(const char *subdir) {
    DIR *dir = opendir(subdir);
    if (!dir) return 0;
    int cnt = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        char *name = ent->d_name;
        size_t len = strlen(name);
        if (len > 4 && strcasecmp(name + len - 4, ".mp3") == 0)
            cnt++;
    }
    closedir(dir);
    return cnt;
}

// ----- 扫描子目录，将 .mp3 全路径填入 table[offset] 开始的位置 -----
static void fill_mp3(const char *subdir, char **table, int offset) {
    DIR *dir = opendir(subdir);
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        char *name = ent->d_name;
        size_t len = strlen(name);
        if (len > 4 && strcasecmp(name + len - 4, ".mp3") == 0) {
            size_t path_len = strlen(subdir) + 1 + len + 1;
            table[offset] = malloc(path_len);
            snprintf(table[offset], path_len, "%s/%s", subdir, name);
            offset++;
        }
    }
    closedir(dir);
}

// ----- qsort 比较函数 -----
static int cmp(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

// ----- 入口：扫描全部媒体目录，构建路由表 -----
static void build_song_table() {
    const char *subdirs[] = { "media/ch1", "media/ch2", "media/ch3" };
    int n = sizeof(subdirs) / sizeof(subdirs[0]);

    song_count = 0;
    for (int i = 0; i < n; i++)
        song_count += count_mp3(subdirs[i]);

    if (song_count == 0) {
        printf("[警告] media/ 下未找到任何 .mp3 文件！\n");
        return;
    }

    song_table = calloc(song_count, sizeof(char *));
    if (!song_table) { perror("calloc"); exit(1); }

    int offset = 0;
    for (int i = 0; i < n; i++) {
        fill_mp3(subdirs[i], song_table, offset);
        offset += count_mp3(subdirs[i]);
    }

    qsort(song_table, song_count, sizeof(char *), cmp);

    printf("[媒体扫描] 共发现 %d 首歌曲:\n", song_count);
    for (int i = 0; i < song_count; i++)
        printf("  %2d: %s\n", i + 1, song_table[i]);
}

// ----- 释放路由表 -----
static void free_song_table() {
    for (int i = 0; i < song_count; i++)
        free(song_table[i]);
    free(song_table);
}

// ========== 主程序 ==========
int main() {
    setlinebuf(stdout);
    
    // 设置信号处理
    signal(SIGPIPE, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    
    printf("======================================\n");
    printf("[云端曲库] TCP 流媒体点播服务器 启动！\n");
    printf("[云端曲库] 正在扫描 media/ 目录...\n");
    printf("======================================\n");

    build_song_table();
    if (song_count == 0) {
        printf("[错误] 曲库为空，服务器退出。\n");
        return 1;
    }

    printf("\n[云端曲库] 正在端口 %d 等待车机连接...\n", SERVER_PORT);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket 创建失败");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind 失败");
        close(server_fd);
        return 1;
    }
    
    if (listen(server_fd, 10) < 0) {
        perror("listen 失败");
        close(server_fd);
        return 1;
    }

    // ⭐ 主循环 - 无限循环，永远不会退出
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            perror("accept 失败");
            continue;  // 继续等待下一个连接
        }

        printf("\n[系统提示] 车载客户端 (%s) 已接入！\n", inet_ntoa(client_addr.sin_addr));

        char request[128] = {0};
        int recv_len = recv(client_fd, request, sizeof(request) - 1, 0);
        if (recv_len <= 0) {
            printf("[错误] 接收请求失败或客户端断开\n");
            close(client_fd);
            continue;
        }
        request[recv_len] = 0;
        
        // 去除换行符
        for (int i = 0; i < recv_len; i++) {
            if (request[i] == '\n' || request[i] == '\r') {
                request[i] = 0;
                break;
            }
        }
        
        printf("[系统提示] 客户点播了歌曲: 编号 %s\n", request);

        int song_idx = atoi(request) - 1;
        if (song_idx < 0 || song_idx >= song_count) {
            printf("[错误] 无效的歌曲编号: %s (有效范围: 1-%d)\n", request, song_count);
            close(client_fd);
            continue;
        }
        printf("[歌曲路由] 编号 %d -> %s\n", song_idx + 1, song_table[song_idx]);

        int file_fd = open(song_table[song_idx], O_RDONLY);
        if (file_fd < 0) {
            perror("打开歌曲文件失败");
            close(client_fd);
            continue;
        }

        struct stat file_stat;
        fstat(file_fd, &file_stat);
        printf("[极速引擎] 歌曲大小: %ld 字节。正在 sendfile 零拷贝推送...\n", file_stat.st_size);

        off_t offset = 0;
        ssize_t remaining = file_stat.st_size;
        while (remaining > 0) {
            ssize_t sent = sendfile(client_fd, file_fd, &offset, remaining);
            if (sent <= 0) {
                if (sent < 0) perror("sendfile 传输出错");
                break;
            }
            remaining -= sent;
        }
        printf("[极速引擎] 传输完成！\n");

        close(file_fd);
        close(client_fd);
        // ⭐ 继续循环，等待下一个客户端连接
    }

    // 正常情况不会执行到这里
    free_song_table();
    close(server_fd);
    return 0;
}
