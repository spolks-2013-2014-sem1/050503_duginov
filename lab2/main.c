#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include "../spolks_lib/sockets.c"

#define bufSize 100

int main(int argc, char *argv[])
{
    char hostName[] = "localhost";
    unsigned int port = 5734;
    struct sockaddr_in sin;
    int socketDescriptor;

    if ((socketDescriptor =
         createTcpServerSocket(hostName, port, &sin, 5)) == -1) {
        printf("Creation socket error\n");
        return 1;
    }

    char buf[bufSize];

    while (1) {
        struct sockaddr_in remote;
        socklen_t rlen = sizeof(remote);
        int remoteSocketDescriptor =
            accept(socketDescriptor, (struct sockaddr *) &remote, &rlen);

        while (1) {
            int numberOfBytesRead =
                recv(remoteSocketDescriptor, buf, bufSize, 0);
            if (numberOfBytesRead <= 0)
                break;
            else {
                printf("%.*s", numberOfBytesRead, buf);
                fflush(stdout);
                send(remoteSocketDescriptor, buf, numberOfBytesRead, 0);
            }
        }
        close(remoteSocketDescriptor);
    }

    return 0;
}
