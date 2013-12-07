
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

void sendFileTCP(char *serverName, unsigned int port, char *filePath);
void sendFileUDP(char *serverName, unsigned int port, char *filePath);

int TcpSocketDescr = -1;

void intHandler(int signo)
{
    if (TcpSocketDescr != -1)
        close(TcpSocketDescr);

    _exit(0);
}


int main(int argc, char *argv[])
{
    if (argc < 4 || argc > 5) {
        fprintf(stderr, "usage: main <host> <port> <filePath> [-u]\n");
        return 1;
    }
    // Change SIGINT action
    struct sigaction intSignal;
    intSignal.sa_handler = intHandler;
    sigaction(SIGINT, &intSignal, NULL);

    if (argc == 4)
        sendFileTCP(argv[1], atoi(argv[2]), argv[3]);
    else
        sendFileUDP(argv[1], atoi(argv[2]), argv[3]);

    return 0;
}


void sendFileTCP(char *serverName, unsigned int serverPort, char *filePath)
{
    struct sockaddr_in serverAddress;

    if ((TcpSocketDescr =
         createTcpSocket(serverName, serverPort, &serverAddress)) == -1) {
        fprintf(stderr, "Creation socket error\n");
        exit(EXIT_FAILURE);
    }

    if (connect
        (TcpSocketDescr, (struct sockaddr *) &serverAddress,
         sizeof(serverAddress)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    FILE *file = fopen(filePath, "r+");
    if (file == NULL) {
        perror("Open file error");
        exit(EXIT_FAILURE);
    }

    long fileSize = GetFileSize(file);

    char replyBuf[replyBufSize];
    sprintf(replyBuf, "%s:%ld", basename(filePath), fileSize);

    // Send file name and file size
    if (send(TcpSocketDescr, replyBuf, sizeof(replyBuf), 0) == -1) {
        perror("Send error");
        exit(EXIT_FAILURE);
    }

    char buf[bufSize];
    long totalBytesSent = 0;
    size_t bytesRead;

    int middle = (fileSize / bufSize) / 2;
    if (middle == 0)
        middle = 1;

    // Sending file
    printf("Start sending file.\n");
    int i = 0;
    while (totalBytesSent < fileSize) {
        bytesRead = fread(buf, 1, sizeof(buf), file);
        int sentBytes = send(TcpSocketDescr, buf, bytesRead, 0);
        if (sentBytes < 0) {
            perror("Sending error\n");
            exit(EXIT_FAILURE);
        }
        totalBytesSent += sentBytes;

        // Send OOB data in the middle of sending file
        if (++i == middle) {
            printf("Sent OOB byte. Total bytes sent: %ld\n",
                   totalBytesSent);
            sentBytes = send(TcpSocketDescr, "!", 1, MSG_OOB);
            if (sentBytes < 0) {
                perror("Sending error");
                exit(EXIT_FAILURE);
            }
        }
    }
    printf("Sending file completed. Total bytes sent: %ld\n",
           totalBytesSent);
    close(TcpSocketDescr);
    fclose(file);
}

void sendFileUDP(char *serverName, unsigned int serverPort, char *filePath)
{
    struct sockaddr_in serverAddress;
    int UdpSocketDescr;

    if ((UdpSocketDescr =
         createUdpSocket(serverName, serverPort, &serverAddress)) == -1) {
        fprintf(stderr, "Creation socket error\n");
        exit(EXIT_FAILURE);
    }
    // Set default address
    if (connect
        (UdpSocketDescr, (struct sockaddr *) &serverAddress,
         sizeof(serverAddress)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    struct timeval tv;
    tv.tv_sec = 30;             // 30 Secs Timeout
    tv.tv_usec = 0;
    setsockopt(UdpSocketDescr, SOL_SOCKET, SO_RCVTIMEO, (char *) &tv,
               sizeof(struct timeval));

    char requestBuf[replyBufSize];
    unsigned char flagBuf;

    FILE *file = fopen(filePath, "r+");
    if (file == NULL) {
        perror("Open file error");
        exit(EXIT_FAILURE);
    }

    long fileSize = GetFileSize(file);

    sprintf(requestBuf, "%s:%ld", basename(filePath), fileSize);
    int bytesTransmitted =
        send(UdpSocketDescr, requestBuf, sizeof(requestBuf), 0);
    if (bytesTransmitted < 0) {
        perror("send");
        exit(EXIT_FAILURE);
    }
    bytesTransmitted = recv(UdpSocketDescr, &flagBuf, sizeof(flagBuf), 0);
    if (bytesTransmitted == -1 || flagBuf != ACK) {
        fprintf(stderr, "No server response\n");
        exit(EXIT_FAILURE);
    }

    printf("Start sending file.\n");
    char buf[bufSize];
    long totalBytesSent = 0;
    size_t bytesRead;

    // Sending file
    while (totalBytesSent < fileSize) {

        bytesRead = fread(buf, 1, sizeof(buf), file);
        int bytesTransmitted = send(UdpSocketDescr, buf, bytesRead, 0);
        if (bytesTransmitted < 0) {
            perror("Sending error\n");
            exit(EXIT_FAILURE);
        }
        totalBytesSent += bytesTransmitted;

        bytesTransmitted =
            recv(UdpSocketDescr, &flagBuf, sizeof(flagBuf), 0);
        if (bytesTransmitted == -1 || flagBuf != ACK) {
            fprintf(stderr, "No server response\n");
            exit(1);
        }
    }
    printf("Sending file completed. Total bytes sent: %ld\n",
           totalBytesSent);
}
