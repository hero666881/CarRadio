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
#include "proto.h"

// ========== 动态歌曲路由表 ==========
char  **song_table = NULL;
int     song_count = 0;

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
            // 分配 "media/ch1/xxx.mp3" 格式的路径
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

    // 第一遍：数总数
    song_count = 0;
    for (int i = 0; i < n; i++)
        song_count += count_mp3(subdirs[i]);

    if (song_count == 0) {
        printf("[警告] media/ 下未找到任何 .mp3 文件！\n");
        return;
    }

    // 分配指针数组
    song_table = calloc(song_count, sizeof(char *));
    if (!song_table) { perror("calloc"); exit(1); }

    // 第二遍：填充
    int offset = 0;
    for (int i = 0; i < n; i++) {
        fill_mp3(subdirs[i], song_table, offset);
        offset += count_mp3(subdirs[i]);  // 再次计数来推进（简单可靠）
    }

    // 按文件名字母序排列，保证编号稳定
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
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(SERVER_PORT);

    bind(server_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    listen(server_fd, 10);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

        printf("\n[系统提示] 车载客户端 (%s) 已接入！\n", inet_ntoa(client_addr.sin_addr));

        char request[128] = {0};
        recv(client_fd, request, sizeof(request), 0);
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
    }

    free_song_table();
    close(server_fd);
    return 0;
}
