CC=gcc
CFLAGS=-Wall
LIBS=-lpthread -lm

# ── 音乐播放器服务端 ──
MUSIC_SRC=musicd.c
MUSIC_TARGET=music_server

# ── 音乐播放器客户端 ──
MCLIENT_SRC=music_client.c
MCLIENT_TARGET=music_player

# ── GPS定位服务端 ──
GPS_DIR=final_map

.PHONY: all music gps clean rebuild run

all: music music_player gps

music: $(MUSIC_TARGET)

music_player: $(MCLIENT_SRC)
	$(CC) $(CFLAGS) $(MCLIENT_SRC) -o $(MCLIENT_TARGET)

gps:
	$(MAKE) -C $(GPS_DIR)

$(MUSIC_TARGET): $(MUSIC_SRC)
	$(CC) $(CFLAGS) $(MUSIC_SRC) -o $(MUSIC_TARGET)

clean:
	rm -f $(MUSIC_TARGET) $(MCLIENT_TARGET)
	$(MAKE) -C $(GPS_DIR) clean

rebuild: clean all
