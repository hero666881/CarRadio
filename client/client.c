#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdarg.h>
#include "proto.h"
#include <dirent.h>

#define MAX_SONGS 128

/* local song table */
static char *local_songs[MAX_SONGS];
static int local_count = 0;

#define UI_CMD_PIPE      "/tmp/ui_cmd"
#define MPLAYER_CMD_PIPE "/tmp/mplayer_cmd"
#define TMP_SONG_FILE    "/tmp/carradio_song.mp3"
#define STATUS_PIPE      "/tmp/carradio_status"

static pid_t g_mplayer = 0;

static void on_signal(int sig) {
    if (g_mplayer > 0) kill(g_mplayer, SIGKILL);
    unlink(TMP_SONG_FILE);
    _exit(0);
}

void create_fifo(const char *path) {
    if (access(path, F_OK) == -1)
        mkfifo(path, 0666);
}

// 返回: 1=play:N(target已设), 2=next, 3=quit, 0=已转发给mplayer, -1=无效
static void write_status(int fd, const char *fmt, ...) __attribute__((format(printf,2,3)));
/* scan local mp3 dirs */
static int count_mp3_local(const char *subdir) {
    DIR *dir = opendir(subdir);
    if (!dir) return 0;
    int cnt = 0; struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        char *name = ent->d_name; size_t len = strlen(name);
        if (len > 4 && strcasecmp(name + len - 4, ".mp3") == 0) cnt++;
    }
    closedir(dir); return cnt;
}

static void fill_mp3_local(const char *subdir, char **table, int offset) {
    DIR *dir = opendir(subdir);
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        char *name = ent->d_name; size_t len = strlen(name);
        if (len > 4 && strcasecmp(name + len - 4, ".mp3") == 0) {
            size_t plen = strlen(subdir) + 1 + len + 1;
            table[offset] = malloc(plen);
            snprintf(table[offset], plen, "%s/%s", subdir, name);
            offset++;
        }
    }
    closedir(dir);
}

static int mp3cmp_local(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static void build_local_table(void) {
    const char *dirs[] = { "media/ch1", "media/ch2", "media/ch3" };
    int n = sizeof(dirs)/sizeof(dirs[0]);
    local_count = 0;
    for (int i = 0; i < n; i++) local_count += count_mp3_local(dirs[i]);
    if (!local_count) return;
    int off = 0;
    for (int i = 0; i < n; i++) {
        fill_mp3_local(dirs[i], local_songs, off);
        off += count_mp3_local(dirs[i]);
    }
    qsort(local_songs, local_count, sizeof(char*), mp3cmp_local);
}

static void free_local_table(void) {
    for (int i = 0; i < local_count; i++) free(local_songs[i]);
}

/* play local mp3 directly (no server) */
static void play_local(int idx, int mplayer_fd, int status_fd) {
    if (idx < 0 || idx >= local_count) return;
    printf("\n[local] play: %s\n", local_songs[idx]);
    write_status(status_fd, "LOCAL_PLAYING:%d", idx + 1);
    g_mplayer = fork();
    if (g_mplayer == 0) {
        char input_arg[256];
        sprintf(input_arg, "file=%s", MPLAYER_CMD_PIPE);
        execlp("mplayer", "mplayer", "-really-quiet", "-ao", "alsa",
               "-slave", "-input", input_arg, local_songs[idx], NULL);
        exit(1);
    }
}

static void write_status(int fd, const char *fmt, ...) {
    if (fd < 0) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[len++] = '\n';
    write(fd, buf, len);
}

static int parse_cmd(char *raw, int n, int *target, int mplayer_fd, int verbose) {
    raw[strcspn(raw, "\r\n")] = 0;
    if (verbose) { printf("[诊断] UI管道收到 %d 字节: \"%s\"\n", n, raw); fflush(stdout); }

    if (strcmp(raw, "next") == 0) {
        printf("\n[总管中枢] 切歌 -> 下一首\n");
        return 2;
	}
    
    if (strncmp(raw, "play:", 5) == 0) {
        int t = atoi(raw + 5);
        if (t > 0) { *target = t; printf("\n[总管中枢] 点歌 -> #%d\n", t); return 1; }
        printf("[总管中枢] 无效编号: %s\n", raw + 5);
        return -1;
    }
    if (strcmp(raw, "quit") == 0 || strcmp(raw, "exit") == 0) {
        printf("\n[总管中枢] 收到退出指令，关闭系统...\n");
        return 3;
    }
    strcat(raw, "\n");
    write(mplayer_fd, raw, strlen(raw));
    printf("[总管中枢] 转发: %s", raw);
    return 0;
}

int main(int argc, char *argv[]) {
    int verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);

    printf("======================================\n");
    printf("[车载中控] TCP 预加载点播引擎 启动！\n");
    printf("[车载中控] 指令: play:N=点歌  next=下一首  pause=暂停\n");
    printf("[车载中控]       volume=N=音量  quit=退出\n");
    printf("======================================\n");

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    create_fifo(UI_CMD_PIPE);
    create_fifo(MPLAYER_CMD_PIPE);

    int ui_fd = open(UI_CMD_PIPE,      O_RDONLY | O_NONBLOCK);
    int mplayer_ctrl_fd = open(MPLAYER_CMD_PIPE, O_RDWR);
    create_fifo(STATUS_PIPE);
    int status_fd = open(STATUS_PIPE, O_RDWR);

    int current_song = 1, running = 1, online_mode = 1;
    pid_t mplayer_pid = 0;

    build_local_table();
    printf("[local] %d local songs\n", local_count);

    while (running) {
        if (!online_mode) { goto local_play; }
        printf("\n[系统状态] 当前歌曲 #%d，正在连接服务器...\n", current_song);

        char song_req[16];
        snprintf(song_req, sizeof(song_req), "%d", current_song);

        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port   = htons(SERVER_PORT);
        inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

        if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            fprintf(stderr, "[错误] 连接服务器失败 (errno=%d: %s), 5秒后重试\n",
                    errno, strerror(errno)); fflush(stderr);
            close(sockfd);
            fprintf(stderr, "[!] server fail, switch to local\n");
            online_mode = 0;
            write_status(status_fd, "LOCAL_MODE");
            continue;
        }

        send(sockfd, song_req, strlen(song_req), 0);
        printf("[网络模块] 已向服务器点播歌曲 #%s\n", song_req);

        /* ---- 阶段1: 下载到本地 ---- */
        printf("[下载引擎] 正在预加载歌曲到本地缓存...\n");
        write_status(status_fd, "DOWNLOADING:%d", current_song);
        int tmp_fd = open(TMP_SONG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (tmp_fd < 0) { perror("创建缓存文件失败"); close(sockfd); continue; }

        char audio_buf[MAX_MSG_SIZE], cmd_buf[128];
        int downloading = 1, download_aborted = 0;
        long total_bytes = 0;

        while (downloading) {
            fd_set fds; FD_ZERO(&fds);
            FD_SET(sockfd, &fds); FD_SET(ui_fd, &fds);
            int mf = (sockfd > ui_fd) ? sockfd : ui_fd;

            if (select(mf + 1, &fds, NULL, NULL, NULL) < 0) { break; }

            if (FD_ISSET(ui_fd, &fds)) {
                memset(cmd_buf, 0, sizeof(cmd_buf));
                int n = read(ui_fd, cmd_buf, sizeof(cmd_buf) - 1);
                if (n > 0) {
                    int target;
                    int act = parse_cmd(cmd_buf, n, &target, mplayer_ctrl_fd, verbose);
                    if (act == 1)      { current_song = target; download_aborted = 1; }
                    else if (act == 2) { current_song++; if (current_song > 99) current_song = 1; download_aborted = 1; }
                    else if (act == 3) { running = 0; download_aborted = 1; }
                    if (download_aborted) {
                        close(tmp_fd); unlink(TMP_SONG_FILE); downloading = 0;
                    }
                }
            }

            if (downloading && FD_ISSET(sockfd, &fds)) {
                int br = recv(sockfd, audio_buf, sizeof(audio_buf), 0);
                if (br > 0) {
                    write(tmp_fd, audio_buf, br);
                    total_bytes += br;
                    if (total_bytes % (512*1024) < (long)sizeof(audio_buf)) {
                        printf("\r[下载引擎] 已预加载: %ld KB", total_bytes / 1024); fflush(stdout);
                    }
                } else {
                    printf("\n[下载引擎] 预加载完成 (共 %ld MB)\n", total_bytes/(1024*1024));
                    downloading = 0;
                }
            }
        }
        close(sockfd); close(tmp_fd);
        if (!running) break;
        if (download_aborted || total_bytes == 0) { 
            write_status(status_fd, "IDLE");
            printf("[系统提示] 跳过，准备下一首...\n"); 
            continue; 
        }

        /* ---- 阶段2: 播放 ---- */
        printf("[播放引擎] 开始播放...\n");
        write_status(status_fd, "PLAYING:%d", current_song);
        
        // 增加小延迟，确保音频设备释放
        usleep(100000);
        
        mplayer_pid = fork(); g_mplayer = mplayer_pid;
        if (mplayer_pid == 0) {
            close(ui_fd); close(mplayer_ctrl_fd);
            char input_arg[256];
            sprintf(input_arg, "file=%s", MPLAYER_CMD_PIPE);
            execlp("mplayer", "mplayer", "-really-quiet", "-ao", "alsa",
                   "-slave", "-input", input_arg, TMP_SONG_FILE, NULL);
            exit(1);
        }
        if (verbose) { printf("[诊断] mplayer PID=%d (存活=%d)\n", mplayer_pid, kill(mplayer_pid,0)==0); fflush(stdout); }

        printf("[总管中枢] 播放中，等待 UI 指令...\n");
        int playing = 1;
        while (playing) {
            fd_set fds; struct timeval tv = {0, 500000};
            FD_ZERO(&fds); FD_SET(ui_fd, &fds);
            if (select(ui_fd + 1, &fds, NULL, NULL, &tv) > 0) {
                memset(cmd_buf, 0, sizeof(cmd_buf));
                int n = read(ui_fd, cmd_buf, sizeof(cmd_buf) - 1);
                if (n > 0) {
                    int target;
                    int act = parse_cmd(cmd_buf, n, &target, mplayer_ctrl_fd, verbose);
                    if (act == 1)      { current_song = target; kill(mplayer_pid, SIGKILL); playing = 0; }
                    else if (act == 2) { current_song++; if (current_song > 99) current_song = 1; kill(mplayer_pid, SIGKILL); playing = 0; }
                    else if (act == 3) { running = 0;              kill(mplayer_pid, SIGKILL); playing = 0; }
                }
            }
            
            // 检查 mplayer 是否结束
            int status;
            pid_t result = waitpid(mplayer_pid, &status, WNOHANG);
            if (result > 0) {
                printf("\n[系统提示] 歌曲播放完毕。\n"); 
                write_status(status_fd, "IDLE"); 
                playing = 0;
                // 发送切歌信号给UI
                write_status(status_fd, "NEXT_SONG");
            }
        }

        if (mplayer_pid > 0) { 
            kill(mplayer_pid, SIGKILL); 
            waitpid(mplayer_pid, NULL, 0); 
            g_mplayer = 0; 
        }
        unlink(TMP_SONG_FILE);
        printf("[系统提示] 缓存已清理。\n");
    }

local_play:
    while (!online_mode && running) {
        char lbuf[128];
        if (current_song < 1 || current_song > local_count) current_song = 1;
        printf("\n[local] #%d\n", current_song);
        play_local(current_song - 1, mplayer_ctrl_fd, status_fd);
        pid_t lm = g_mplayer;
        int lp = 1;
        while (lp) {
            fd_set lfds; struct timeval ltv = {0, 500000};
            FD_ZERO(&lfds); FD_SET(ui_fd, &lfds);
            if (select(ui_fd + 1, &lfds, NULL, NULL, &ltv) > 0) {
                memset(lbuf, 0, sizeof(lbuf));
                int n = read(ui_fd, lbuf, sizeof(lbuf) - 1);
                if (n > 0) {
                    if (strncmp(lbuf, "mode:online", 11) == 0) {
                        online_mode = 1; write_status(status_fd, "ONLINE_MODE");
                        if (lm > 0) kill(lm, SIGKILL);
                        break;
                    }
                    int target;
                    int act = parse_cmd(lbuf, n, &target, mplayer_ctrl_fd, verbose);
                    if (act == 1) { current_song = target; kill(lm, SIGKILL); lp = 0; }
                    else if (act == 2) { current_song++; if (current_song > local_count) current_song = 1; kill(lm, SIGKILL); lp = 0; }
                    else if (act == 3) { running = 0; kill(lm, SIGKILL); lp = 0; }
                }
            }
            if (waitpid(lm, NULL, WNOHANG) > 0) {
                printf("\n[local] finished\n"); write_status(status_fd, "IDLE"); lp = 0;
            }
        }
        if (lm > 0) { kill(lm, SIGKILL); waitpid(lm, NULL, 0); g_mplayer = 0; }
    }

    write_status(status_fd, "QUIT");
    close(status_fd);
    free_local_table();
    printf("[车载中控] 系统已安全退出。\n");
    return 0;
}
