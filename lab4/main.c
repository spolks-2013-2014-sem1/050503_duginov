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

void receiveFile(char *hostName, unsigned int port);
void sendFile(char *serverName, unsigned int port, char *filePath);

int clientSocketDescriptor = -1;
int serverSocketDescriptor = -1;
int remoteSocketDescriptor = -1;

void intHandler(int signo)
{
    if (serverSocketDescriptor != -1)
        close(serverSocketDescriptor);
    else
        close(clientSocketDescriptor);

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
        fprintf(stderr,
                "usage 1: main <host> <port>\nusage 2: main <host> <port> <filePath>\n");
        return 1;
    }
    // Change SIGINT action
    struct sigaction intSignal;
    intSignal.sa_handler = intHandler;
    sigaction(SIGINT, &intSignal, NULL);

    if (argc == 3)
        receiveFile(argv[1], atoi(argv[2]));
    else
        sendFile(argv[1], atoi(argv[2]), argv[3]);

    return 0;
}


void receiveFile(char *hostName, unsigned int port)
{
    struct sockaddr_in sin;

    if ((serverSocketDescriptor =
         createTcpServerSocket(hostName, port, &sin, 5)) == -1) {
        printf("Creation socket error\n");
        exit(EXIT_FAILURE);
    }

    char replyBuf[replyBufSize], buf[bufSize];

    while (1) {
        printf("\nAwaiting connections...\n");
        remoteSocketDescriptor =
            accept(serverSocketDescriptor, NULL, NULL);

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


void sendFile(char *serverName, unsigned int serverPort, char *filePath)
{
    struct sockaddr_in serverAddress;

    if ((clientSocketDescriptor =
         createTcpSocket(serverName, serverPort, &serverAddress)) == -1) {
        fprintf(stderr, "Creation socket error\n");
        exit(EXIT_FAILURE);
    }

    if (connect
        (clientSocketDescriptor, (struct sockaddr *) &serverAddress,
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
    if (send(clientSocketDescriptor, replyBuf, sizeof(replyBuf), 0) == -1) {
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
        int sendBytes = send(clientSocketDescriptor, buf, bytesRead, 0);
        if (sendBytes < 0) {
            perror("Sending error\n");
            exit(EXIT_FAILURE);
        }
        totalBytesSent += sendBytes;

        // Send OOB data in the middle of sending file
        if (++i == middle) {
            printf("Sent OOB byte. Total bytes sent: %ld\n",
                   totalBytesSent);
            sendBytes = send(clientSocketDescriptor, "!", 1, MSG_OOB);
            if (sendBytes < 0) {
                perror("Sending error");
                exit(EXIT_FAILURE);
            }
        }
    }
    printf("Sending file completed. Total bytes sent: %ld\n",
           totalBytesSent);
    close(clientSocketDescriptor);
    fclose(file);
}
