
#include "../spolks_lib/utils.c"
#include "../spolks_lib/sockets.c"

#include <sys/types.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <libgen.h>
#include <fcntl.h>
#include <errno.h>

#define replyBufSize 256
#define bufSize 4096

const unsigned char ACK = 1;

void receiveFileTCP(char *serverName, unsigned int port);
void receiveFileUDP(char *serverName, unsigned int port);

int TcpServerDescr = -1;

void intHandler(int signo)
{
    if (TcpServerDescr != -1)
        close(TcpServerDescr);

    _exit(0);
}

int oobFlag = 0;
void urgHandler(int signo)
{
    oobFlag = 1;
}

int main(int argc, char *argv[])
{
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: main <host> <port> [-u]\n");
        return 1;
    }
    // Change SIGINT action
    struct sigaction intSignal;
    intSignal.sa_handler = intHandler;
    sigaction(SIGINT, &intSignal, NULL);

    if (argc == 3)
        receiveFileTCP(argv[1], atoi(argv[2]));
    else
        receiveFileUDP(argv[1], atoi(argv[2]));

    return 0;
}


void receiveFileTCP(char *hostName, unsigned int port)
{
    struct sockaddr_in sin;

    if ((TcpServerDescr =
         createTcpServerSocket(hostName, port, &sin, 5)) == -1) {
        printf("Creation socket error\n");
        exit(EXIT_FAILURE);
    }

    char replyBuf[replyBufSize], buf[bufSize];

    while (1) {
        printf("\nAwaiting connections...\n");
        int remoteSocketDescriptor = accept(TcpServerDescr, NULL, NULL);

        struct sigaction urgSignal;
        urgSignal.sa_handler = urgHandler;
        sigaction(SIGURG, &urgSignal, NULL);

        if (fcntl(remoteSocketDescriptor, F_SETOWN, getpid()) < 0) {
            perror("fcntl()");
            exit(-1);
        }
        // Receive file name and file size
        if (ReceiveToBuf
            (remoteSocketDescriptor, replyBuf, sizeof(replyBuf)) <= 0) {
            close(remoteSocketDescriptor);
            fprintf(stderr, "Error receiving file name and file size\n");
            continue;
        }
        char *fileName = strtok(replyBuf, ":");
        if (fileName == NULL) {
            close(remoteSocketDescriptor);
            fprintf(stderr, "Bad file name\n");
            continue;
        }
        char *size = strtok(NULL, ":");
        if (size == NULL) {
            close(remoteSocketDescriptor);
            fprintf(stderr, "Bad file size\n");
            continue;
        }
        long fileSize = atoi(size);

        printf("File size: %ld, file name: %s\n", fileSize, fileName);

        FILE *file = CreateReceiveFile(fileName, "Received_files");
        if (file == NULL) {
            perror("Create file error");
            exit(EXIT_FAILURE);
        }
        // Receiving file   
        int recvSize;
        long totalBytesReceived = 0;

        while (totalBytesReceived < fileSize) {

            if (sockatmark(remoteSocketDescriptor) == 1 && oobFlag == 1) {
                printf("Receive OOB byte. Total bytes received: %ld\n",
                       totalBytesReceived);

                char oobBuf;
                int n = recv(remoteSocketDescriptor, &oobBuf, 1, MSG_OOB);
                if (n == -1)
                    fprintf(stderr, "receive OOB error\n");
                oobFlag = 0;
            }

            recvSize = recv(remoteSocketDescriptor, buf, sizeof(buf), 0);

            if (recvSize > 0) {
                totalBytesReceived += recvSize;
                fwrite(buf, 1, recvSize, file);
            } else if (recvSize == 0) {
                printf("Received EOF\n");
                break;
            } else {
                if (errno == EINTR)
                    continue;
                else {
                    perror("receive error");
                    break;
                }
            }
        }
        fclose(file);
        printf("Receiving file completed. %ld bytes received.\n",
               totalBytesReceived);
        close(remoteSocketDescriptor);
    }
}

void receiveFileUDP(char *hostName, unsigned int port)
{
    struct sockaddr_in sin;
    int UdpServerDescr;

    if ((UdpServerDescr =
         createUdpServerSocket(hostName, port, &sin)) == -1) {
        printf("Creation socket error\n");
        exit(EXIT_FAILURE);
    }

    struct timeval timeOut, noTimeOut;
    timeOut.tv_sec = 30;        //30 Secs Timeout
    timeOut.tv_usec = 0;
    noTimeOut.tv_sec = 0;
    noTimeOut.tv_usec = 0;

    struct sockaddr_in remote;
    socklen_t rlen = sizeof(remote);
    char requestBuf[replyBufSize], buf[bufSize];
    int bytesTransmitted;

    while (1) {
        setsockopt(UdpServerDescr, SOL_SOCKET, SO_RCVTIMEO, (char *) &noTimeOut, sizeof(struct timeval));       // disable timeout
        printf("\nAwaiting connections...\n");

        bytesTransmitted =
            recvfrom(UdpServerDescr, requestBuf, sizeof(requestBuf), 0,
                     (struct sockaddr *) &remote, &rlen);

        if (bytesTransmitted < 0) {
            perror("Receive");
            continue;
        }

        char *fileName = strtok(requestBuf, ":");
        if (fileName == NULL) {
            fprintf(stderr, "Bad file name\n");
            continue;
        }
        char *size = strtok(NULL, ":");
        if (size == NULL) {
            fprintf(stderr, "Bad file size\n");
            continue;
        }
        long fileSize = atoi(size);
        printf("File size: %ld, file name: %s\n", fileSize, fileName);

        FILE *file = CreateReceiveFile(fileName, "Received_files");
        if (file == NULL) {
            perror("Create file error");
            exit(EXIT_FAILURE);
        }

        bytesTransmitted =
            sendto(UdpServerDescr, &ACK, sizeof(ACK), 0,
                   (struct sockaddr *) &remote, rlen);
        if (bytesTransmitted < 0) {
            perror("send");
            continue;
        }
        // Receiving file   
        long totalBytesReceived = 0;

        setsockopt(UdpServerDescr, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeOut, sizeof(struct timeval)); // set timeout

        while (totalBytesReceived < fileSize) {
            bytesTransmitted =
                recvfrom(UdpServerDescr, buf, sizeof(buf), 0,
                         (struct sockaddr *) &remote, &rlen);
            if (bytesTransmitted > 0) {
                totalBytesReceived += bytesTransmitted;
                fwrite(buf, 1, bytesTransmitted, file);

                bytesTransmitted =
                    sendto(UdpServerDescr, &ACK, sizeof(ACK), 0,
                           (struct sockaddr *) &remote, rlen);
                if (bytesTransmitted < 0) {
                    perror("send");
                    exit(EXIT_FAILURE);
                }
            } else {
                perror("receive error");
                break;
            }
        }
        printf("Receiving file completed. %ld bytes received.\n",
               totalBytesReceived);
        fclose(file);
    }
}
