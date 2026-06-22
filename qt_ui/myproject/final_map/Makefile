CC=gcc

TARGET=gps_car

SRC=main.c \
	server.c \
	client.c \
	threadpool.c \
	car.c \
	gps.c \
	distance.c \
	log.c

CFLAGS=-Wall

LIBS=-lpthread -lm

all: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p data
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)

rebuild: clean all

run: all
	./$(TARGET)
