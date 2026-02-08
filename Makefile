CC = gcc
CFLAGS = -O3
LDFLAGS = -lm

SOURCES = main.c
TARGET = zfbv


all:
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS)

