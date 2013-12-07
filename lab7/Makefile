CC_c=gcc
CC_cpp=g++

CFLAGS_c=-Wall -std=c99 -D_GNU_SOURCE
CFLAGS_cpp=-Wall -std=c++0x


all: 
	$(CC_c) $(CFLAGS_c) client.c -o client
	$(CC_cpp) $(CFLAGS_cpp) server.cpp -o server -lpthread

clean:
	rm -f client
	rm -f server
	rm -f client.c~
	rm -f server.cpp~
	rm -f Makefile~
	rm -rf Received_files
