.PHONY: all clean

# compiler name
CC=gcc

# place options like -O2, -g here
CFLAGS=-g -Wall

all: boss

#boss: boss.c boss.h
#	$(CC) -o boss $(CFLAGS) boss.h boss.c

boss: boss.c
	$(CC) -o boss $(CFLAGS) boss.c -lssl -lcrypto

clean:
	rm -f boss
