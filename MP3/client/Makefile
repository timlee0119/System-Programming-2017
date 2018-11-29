.PHONY: all clean

# compiler name
CC=gcc

# place options like -O2, -g here
CFLAGS=-g -Wall

all: miner

miner: miner.c
	$(CC) -o miner $(CFLAGS) miner.c -lssl -lcrypto

clean:
	rm -f miner
