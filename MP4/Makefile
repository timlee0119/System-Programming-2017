.PHONY: all clean

# compiler name
CC=g++

# place options like -O2, -g here
CFLAGS=-Wall -std=c++11

all: inf-bonbon-server

inf-bonbon-server: inf-bonbon-server.cpp
	$(CC) -o inf-bonbon-server $(CFLAGS) inf-bonbon-server.cpp -ldl

clean:
	rm -f inf-bonbon-server
