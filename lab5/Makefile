CC=gcc

CFLAGS=-Wall -std=c99 -D_GNU_SOURCE


all: 
	$(CC) $(CFLAGS) client.c -o client
	$(CC) $(CFLAGS) server.c -o server

clean:
	rm -f client
	rm -f server
