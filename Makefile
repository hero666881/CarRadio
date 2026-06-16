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
	rm -f /tmp/car_cmd  # 留着 ui_cmd 和 mplayer_cmd，避免 client fd 失效

.PHONY: all clean
