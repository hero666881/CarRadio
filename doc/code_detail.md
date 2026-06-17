# CarRadio 项目代码详解

> 车载 TCP 智能音乐点播系统 — 完整代码逐层解析

---

## 目录

1. [项目架构概览](#1-项目架构概览)
2. [include/proto.h — 通信协议常量](#2-includeprotoh--通信协议常量)
3. [Makefile — 一键编译配置](#3-makefile--一键编译配置)
4. [server/server.c — 云端曲库服务端](#4-serverserverc--云端曲库服务端)
5. [client/client.c — 车载播放引擎客户端](#5-clientclientc--车载播放引擎客户端)
6. [qt_ui/main.cpp — Qt 图形界面](#6-qt_uimaincpp--qt-图形界面)

---

## 1. 项目架构概览

### 1.1 系统角色与进程

整个系统由 **三个独立进程** 协作完成：

```
+---------------------------+       TCP 8888        +---------------------+
|   server_app              | ◄───────────────────► |   client_app         |
|   (云端曲库服务端)         |    sendfile 零拷贝     |   (车载播放引擎)     |
|   — 扫描 media/ 目录      |                        |   — 预加载下载       |
|   — sendfile 推送 MP3     |                        |   — fork/mplayer 播放 |
+---------------------------+                        +---------+-----------+
                                                                  | 命名管道
                                                                  ├─ /tmp/ui_cmd
                                                                  ├─ /tmp/mplayer_cmd
                                                                  └─ /tmp/carradio_status
                                                          +-------+-----------+
                                                          |   ui_qt           |
                                                          |   (Qt 5 图形界面) |
                                                          |   — 歌单浏览      |
                                                          |   — 播放控制      |
                                                          +-------------------+
```

### 1.2 数据流

| 方向 | 内容 | 协议 |
|---|---|---|
| UI → client | `play:5`, `next`, `pause`, `volume 10` | 命名管道 `/tmp/ui_cmd` |
| client → server | 歌曲编号 `"5"` | TCP 8888 |
| server → client | MP3 二进制流 | sendfile (TCP) |
| client → UI | `PLAYING:5`, `DOWNLOADING`, `IDLE` | 命名管道 `/tmp/carradio_status` |
| client → mplayer | `pause`, `seek 100 2`, `volume -10` | 命名管道 `/tmp/mplayer_cmd` |

---

## 2. include/proto.h — 通信协议常量

```c
/** * @file proto.h 
 * @brief CarRadio 车载收音机共享通信协议常量定义
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
```

| 宏名 | 值 | 作用 |
|---|---|---|
| `SERVER_IP` | `"127.0.0.1"` | 服务器 IP。本地测试用回环地址，部署时改为实际 IP |
| `SERVER_PORT` | `8888` | TCP 监听端口 |
| `CMD_PIPE_PATH` | `"/tmp/car_cmd"` | 历史遗留宏（当前未直接使用，实际改用 `/tmp/ui_cmd`） |
| `MAX_MSG_SIZE` | `4096` | TCP recv() 单次最大读取量 (4KB) |

---

## 3. Makefile — 一键编译配置

```makefile
# 编译器
CC = gcc
CFLAGS  = -Wall -g -I./include

SERVER_BIN = server_app
CLIENT_BIN = client_app

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): server/server.c
	$(CC) $(CFLAGS) $< -o $@

$(CLIENT_BIN): client/client.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
	rm -f /tmp/car_cmd

.PHONY: all clean
```

| 变量/目标 | 说明 |
|---|---|
| `CC = gcc` | C 编译器 |
| `CFLAGS = -Wall -g -I./include` | 抛出所有警告 + 调试符号 + 包含路径 |
| `all` | 同时编译 server_app 和 client_app |
| `make clean` | 删除二进制 + 旧管道文件 `/tmp/car_cmd` |

---

## 4. server/server.c — 云端曲库服务端

### 4.1 整体职责

```
接收客户端 → 解析歌曲编号 → open() 打开 MP3 → sendfile() 零拷贝发送
```

### 4.2 全局变量

```c
char  **song_table = NULL;   // 歌曲路径指针数组 ["media/ch1/a.mp3", ...]
int     song_count = 0;      // 歌曲总数
```

### 4.3 函数详解

#### static void on_signal(int sig)
- **作用**: 信号处理回调
- **SIGPIPE**: 客户端断开时产生，直接 return 忽略，避免进程退出
- **SIGINT/SIGTERM**: 打印提示后 exit(0) 优雅关闭

#### static int count_mp3(const char *subdir)
- **作用**: 统计子目录下 .mp3 文件数量
- **参数**: subdir 如 "media/ch1"
- **流程**: opendir() → readdir() 循环 → 检查 .mp3 后缀 → closedir()
- **返回**: .mp3 数量

| API | 作用 |
|---|---|
| `opendir()` | 打开目录流 |
| `readdir()` | 读取下一项目录条目 |
| `strcasecmp()` | 不区分大小写比较字符串 |
| `closedir()` | 关闭目录流 |

#### static void fill_mp3(const char *subdir, char **table, int offset)
- **作用**: 将目录下所有 .mp3 完整路径填入数组
- **参数**: subdir 目录, table 路径数组, offset 起始下标
- **示例**: fill_mp3("media/ch1", song_table, 0) → song_table[0] = "media/ch1/song.mp3"

#### static int cmp(const void *a, const void *b)
- **作用**: qsort 排序回调，按文件名字母序排列

#### static void build_song_table()
- **作用**: 扫描全部目录构建完整路由表
- **算法**:
  1. 第一遍 count_mp3 统计总数
  2. calloc 分配指针数组
  3. 第二遍 fill_mp3 填充路径
  4. qsort 排序
- **扫描子目录**: media/ch1, media/ch2, media/ch3

#### static void free_song_table()
- **作用**: 释放 song_table 全部动态内存

#### int main() — 主程序
- **阶段 1**: 初始化 — setlinebuf, signal, build_song_table
- **阶段 2**: 创建 TCP 服务端 — socket, bind, listen（端口 8888）
- **阶段 3**: 主循环 `while(1)` — accept, recv 编号, open MP3, sendfile 零拷贝推送

关键：`while(1)` 保证 server 永不退出，持续接受新连接。

### 4.4 核心技术 — sendfile 零拷贝

```c
ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
```

**传统路径**: 硬盘 → 内核缓冲区 → 用户缓冲区 → 内核Socket缓冲区 → 网卡
**sendfile路径**: 硬盘 → 内核缓冲区 → 网卡（直接 DMA，减少两次拷贝和上下文切换）

---

## 5. client/client.c — 车载播放引擎客户端

### 5.1 整体职责

```
连接服务器 → 下载 MP3 到本地 → fork 子进程播放 → select 监听 UI 指令
```

这是整个系统最核心的模块，实现了"预加载下载"和"可中断控制"。

### 5.2 宏定义与全局变量

```c
#define UI_CMD_PIPE      "/tmp/ui_cmd"           // UI → client 指令通道
#define MPLAYER_CMD_PIPE "/tmp/mplayer_cmd"      // client → mplayer 指令通道
#define TMP_SONG_FILE    "/tmp/carradio_song.mp3" // 下载缓存文件
#define STATUS_PIPE      "/tmp/carradio_status"   // client → UI 状态通道

static pid_t g_mplayer = 0;   // 当前 mplayer 子进程 PID
```

### 5.3 三条命名管道

| 管道 | 写端 | 读端 | 内容示例 |
|---|---|---|---|
| `/tmp/ui_cmd` | Qt UI | client_app | `play:5`, `next`, `pause` |
| `/tmp/mplayer_cmd` | client_app | mplayer | `pause`, `seek 100 2` |
| `/tmp/carradio_status` | client_app | Qt UI | `PLAYING:5`, `IDLE`, `QUIT` |

### 5.4 函数详解

#### static void on_signal(int sig)
- 进程退出时 kill mplayer 子进程，删除临时文件，_exit(0)

#### void create_fifo(const char *path)
- 创建命名管道，已存在则跳过
- `mkfifo(path, 0666)` 创建 FIFO

#### static void write_status(int fd, const char *fmt, ...)
- 向 status 管道写入格式化状态，支持 printf 风格可变参数
- 尾部自动加 \n 作为消息结束符

#### static int parse_cmd(char *raw, int n, int *target, int mplayer_fd, int verbose)
- 解析 UI 指令，返回值:
  - 1: play:N 点歌 (*target = N)
  - 2: next 下一首
  - 3: quit/exit 退出
  - 0: 其他指令 (pause/volume 等) 转发给 mplayer
  - -1: 无效指令

#### int main() — 主程序

**整体循环**:
```
while (running) {
  阶段 1: 预加载 (下载)
    connect(server) → send("5")
    select(sockfd, ui_fd) → 下载同时可响应切歌
    下载完毕或被打断

  阶段 2: 播放
    fork() → 子进程 execlp("mplayer", ...)
    select(ui_fd, 0.5s超时) → 监听 UI 指令
    waitpid(mplayer) → 播完自动下一首
    
    kill(mplayer) + waitpid() → 回收子进程
    unlink(临时文件) → 清理缓存
}
```

### 5.5 关键技术详解

**select() 多路复用**: 下载时同时监听网络 socket 和 UI 管道，用户切歌可中断下载。
**fork() + execlp()**: 子进程变身为 mplayer，参数 `-slave -input file=/tmp/mplayer_cmd`。
**waitpid 进程回收**: WNOHANG 非阻塞检查，防止僵尸进程。

---

## 6. qt_ui/main.cpp — Qt 图形界面

### 6.1 整体职责

```
扫描 media/ → 显示歌单 → 用户点击 → write(ui_cmd) → client_app 响应
                                  ↓ 同时
                        read(status) → 更新 UI 状态
```

### 6.2 前端静态函数

| 函数 | 作用 |
|---|---|
| `create_fifo()` | 创建命名管道 |
| `pipe_write()` | 向管道写入指令（尾部加 \n） |
| `scan_dir()` | 扫描目录填充 song_paths 和 song_cat |
| `build_list()` | 扫描全部子目录并排序 |
| `getDur()` | 先 mplayer -identify 获取真实时长，失败则用文件大小估算 |

### 6.3 CarRadio 类

**构造函数**: 创建窗口布局：
- 顶栏 (40px): 标题 + 时间
- 主体: 左侧导航 (180px) + QStackedWidget
- 底部控制栏 (120px): 进度条 + 控制按钮

**导航栏**: 音乐(♪)、视频(▶)、收音(⊙)、蓝牙(β)、导航(⟐)、设置(⚙)

| 方法 | 作用 |
|---|---|
| `playSong(idx)` | 更新 UI + 发 `play:N` 指令 + 启动 fakeTimer |
| `tickProgress()` | 每秒进度条 +1，播完停止 |
| `onProgressSeek()` | 拖动进度条 → 发 `seek N 2` 给 mplayer |
| `onStatus()` | 异步读取 status 管道，更新 UI |

**状态响应**:
| 消息 | UI 行为 |
|---|---|
| `PLAYING:5` | 更新歌曲名、进度条初始化、启动 timer |
| `DOWNLOADING:5` | 专辑封面显示 "↓" |
| `IDLE` | 停止 timer，自动播下一首 |
| `NEXT_SONG` | 切到下一首 |
| `QUIT` | 停止 timer |

### 6.4 底部控制栏

```
歌曲名                        [⏮] [⏯] [⏭]  [🔊-] [🔊+] [🔇]
[=====●============] 00:00 / 03:45
```

| 按钮 | 指令 | 功能 |
|---|---|---|
| ⏮ | playSong(current-1) | 上一曲 |
| ⏯ | pause | 暂停/播放 |
| ⏭ | next | 下一曲 |
| 🔊- | volume -10 | 减小音量 |
| 🔊+ | volume +10 | 增大音量 |
| 🔇 | mute | 静音 |

---


## 7. 新增：本地播放模式

### 7.1 设计目标

在不启动 server_app、没有 TCP 网络的场景下，client_app 能直接播放本地 media/ 目录下的 MP3 文件。

### 7.2 核心改动

#### 双模切换 (online_mode 标志)

```c
int current_song = 1, running = 1, online_mode = 1;
```

- `online_mode = 1`：在线模式，连接服务器下载 → 播放
- `online_mode = 0`：本地模式，直接播本地文件
- 连接失败时自动回退：`online_mode = 0`
- UI 可发 `mode:online` / `mode:local` 手动切换

#### 本地歌曲扫描（与 server.c 逻辑一致）

| 函数 | 说明 |
|---|---|
| `count_mp3_local(subdir)` | 统计子目录 .mp3 数量 |
| `fill_mp3_local(subdir, table, offset)` | 填入完整路径 |
| `mp3cmp_local()` | qsort 排序 |
| `build_local_table()` | 构建完整路由表，扫描 ch1/ch2/ch3 |
| `free_local_table()` | 释放内存 |

#### 本地播放函数

```c
static void play_local(int idx, int mplayer_fd, int status_fd) {
    printf("[local] play: %s\n", local_songs[idx]);
    write_status(status_fd, "LOCAL_PLAYING:%d", idx + 1);
    g_mplayer = fork();
    if (g_mplayer == 0) {
        execlp("mplayer", "mplayer", "-really-quiet", "-ao", "alsa",
               "-slave", "-input", input_arg, local_songs[idx], NULL);
    }
}
```

与在线模式的区别：
- 不经过 TCP 下载，直接传本地路径给 mplayer
- 状态管道发 `LOCAL_PLAYING:N` 而非 `PLAYING:N`

#### 本地播放循环 (local_play: goto 标签)

```
local_play:
  while (online_mode == 0 && running) {
    play_local(current_song)
    while (playing) {
      select(ui_fd) → 监听 UI 指令
      if (mode:online) { online_mode = 1; break; }
      if (play:N / next / quit) → 切歌或退出
      waitpid(mplayer) → 播完自动下一首
    }
  }
```

### 7.3 新增状态消息

| 消息 | 说明 |
|---|---|
| `LOCAL_PLAYING:N` | 正在播放本地第 N 首歌 |
| `LOCAL_MODE` | 已切换到本地模式 |
| `ONLINE_MODE` | 已切换到在线模式 |

### 7.4 新增 UI 指令

| 指令 | 说明 |
|---|---|
| `mode:local` | 切换到本地播放模式 |
| `mode:online` | 切换到在线播放模式 |

### 7.5 使用场景

```bash
# 场景 1：不开 server_app，纯本地播放
cd ~/emb260316/UNIX/CarRadio
./client_app
# → 连接失败 → 自动切换本地模式 → 直接播歌

# 场景 2：在线中切本地
echo "mode:local" > /tmp/ui_cmd

# 场景 3：本地切回在线（需要 server_app 在运行）
echo "mode:online" > /tmp/ui_cmd
```

## 附录: 核心 API 速查

| API | 头文件 | 本项目中用途 |
|---|---|---|
| `socket()` | sys/socket.h | 创建 TCP 连接 |
| `bind()` | sys/socket.h | server 绑定 8888 端口 |
| `listen()` | sys/socket.h | server 进入监听模式 |
| `accept()` | sys/socket.h | server 接受客户端连接 |
| `connect()` | sys/socket.h | client 连接 server |
| `sendfile()` | sys/sendfile.h | 零拷贝发送 MP3 |
| `fork()` | unistd.h | 创建 mplayer 子进程 |
| `execlp()` | unistd.h | 子进程变身为 mplayer |
| `waitpid()` | sys/wait.h | 回收子进程 |
| `mkfifo()` | sys/stat.h | 创建命名管道 |
| `select()` | sys/select.h | I/O 多路复用 |
| `opendir()` | dirent.h | 扫描 media/ 目录 |
| `readdir()` | dirent.h | 遍历目录项 |
| `signal()` | signal.h | 注册信号处理 |

---

*文档生成时间: 2026-06-17*
*CarRadio — 车载 TCP 智能音乐点播系统*

