#ifdef __cplusplus 
extern "C" {
#endif

#include "../spolks_lib/utils.c"
#include "../spolks_lib/sockets.c"

#include <sys/types.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>

#ifdef __cplusplus 
}
#endif

#include <cstdint>
#include <iostream>
#include <map>

#define replyBufSize 256
#define bufSize 4096

using namespace std;

void receiveFileTCP(char *serverName, unsigned int port);
void receiveFileUDP(char *serverName, unsigned int port);

void TCP_Processing(int rsd);
void UDP_Processing(unsigned char *buf, int size,
                    struct sockaddr_in &addr);

int TcpServerDescr = -1;

void intHandler(int signo)
{
    if (TcpServerDescr != -1)
        close(TcpServerDescr);

    _exit(0);
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

    signal(SIGCHLD, SIG_IGN);   // make SIGCHLD ignored to avoid zombies

    if (argc == 3)
        receiveFileTCP(argv[1], atoi(argv[2]));
    else
        receiveFileUDP(argv[1], atoi(argv[2]));

    return 0;
}


//------------------------------------------------//
//------------------TCP SERVER--------------------//
//------------------------------------------------//

void receiveFileTCP(char *hostName, unsigned int port)
{
    struct sockaddr_in sin;

    if ((TcpServerDescr =
         createTcpServerSocket(hostName, port, &sin, 5)) == -1) {
        printf("Creation socket error\n");
        exit(EXIT_FAILURE);
    }

    while (1) {

        int rsd = accept(TcpServerDescr, NULL, NULL);
        if (rsd == -1) {
            perror("Accept");
            exit(EXIT_FAILURE);
        }

        switch (fork()) {
        case -1:
            perror("fork()");
            exit(EXIT_FAILURE);

        case 0:                // child process
            TCP_Processing(rsd);
            return;

        default:               // parrent process
            close(rsd);
            break;
        }
    }
}

void TCP_Processing(int rsd)
{
    char replyBuf[replyBufSize], buf[bufSize];

    // Receive file name and file size
    if (ReceiveToBuf(rsd, replyBuf, sizeof(replyBuf)) <= 0) {
        close(rsd);
        fprintf(stderr, "Error receiving file name and file size\n");
        return;
    }

    char *size = getFileSizePTR(replyBuf, sizeof(replyBuf));
    if (size == NULL) {
        close(rsd);
        fprintf(stderr, "Bad file size\n");
        return;
    }
    long fileSize = atoi(size);

    char *fileName = replyBuf;

    printf("File size: %ld, file name: %s\n", fileSize, fileName);

    FILE *file = CreateReceiveFile(fileName, "Received_files");
    if (file == NULL) {
        perror("Create file error");
        exit(EXIT_FAILURE);
    }
    // Receiving file   
    int recvSize;
    long totalBytesReceived = 0;

    fd_set rset, xset;
    FD_ZERO(&rset);
    FD_ZERO(&xset);

    while (totalBytesReceived < fileSize) {

        FD_SET(rsd, &rset);
        select(rsd + 1, &rset, NULL, &xset, NULL);

        if (FD_ISSET(rsd, &xset)) {
            printf
                ("Received OOB byte. Total bytes of \"%s\" received: %ld\n",
                 fileName, totalBytesReceived);

            char oobBuf;
            int n = recv(rsd, &oobBuf, 1, MSG_OOB);
            if (n == -1)
                fprintf(stderr, "receive OOB error\n");
            FD_CLR(rsd, &xset);
        }

        if (FD_ISSET(rsd, &rset)) {
            recvSize = recv(rsd, buf, sizeof(buf), 0);

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
            FD_SET(rsd, &xset);
        }
    }
    fclose(file);
    printf("Receiving file \"%s\" completed. %ld bytes received.\n",
           fileName, totalBytesReceived);
    close(rsd);

    return;
}


//------------------------------------------------//
//------------------UDP SERVER--------------------//
//------------------------------------------------//

struct fileInfo {
    FILE *file;
    char fileName[256];
};


int UdpServerDescr = -1;

const unsigned char ACK = 1;
const unsigned char END = 2;

map < uint64_t, fileInfo * >filesMap;


void receiveFileUDP(char *hostName, unsigned int port)
{
    struct sockaddr_in sin;

    if ((UdpServerDescr =
         createUdpServerSocket(hostName, port, &sin)) == -1) {
        fprintf(stderr, "Creation socket error\n");
        exit(EXIT_FAILURE);
    }


    struct timeval timeOut = {30, 0}, noTimeOut = {0, 0};

    struct sockaddr_in addr;
    socklen_t rlen = sizeof(addr);
    int recvSize;

    while (1) {
        if (filesMap.size() > 1)
            setsockopt(UdpServerDescr, SOL_SOCKET, SO_RCVTIMEO, &timeOut, sizeof(struct timeval));      // set timeout
        else
            setsockopt(UdpServerDescr, SOL_SOCKET, SO_RCVTIMEO, &noTimeOut, sizeof(struct timeval));    // disable timeout

        unsigned char buf[bufSize];

        recvSize =
            recvfrom(UdpServerDescr, buf, sizeof(buf), 0,
                     (struct sockaddr *) &addr, &rlen);
        if (recvSize < 0) {
            perror("recvfrom()");
            exit(EXIT_FAILURE);
        }

        UDP_Processing(buf, recvSize, addr);
    }
}


void UDP_Processing(unsigned char *buf, int recvSize, struct sockaddr_in &addr)
{
    socklen_t rlen = sizeof(addr);
    int bytesTransmitted;

    uint64_t address =
        IpPortToNumber(addr.sin_addr.s_addr, addr.sin_port);

    map < uint64_t, fileInfo * >::iterator pos =
        filesMap.find(address);

    // client address not found in array
    if (pos == filesMap.end()) {

        char *fileSizeStr = getFileSizePTR((char *) buf, recvSize);
        if (fileSizeStr == NULL) {
            fprintf(stderr, "Bad file size\n");
            return;
        }
        long fileSize = atoi(fileSizeStr);

        char *fileName = (char *) buf;

        printf("File size: %ld, file name: %s\n", fileSize, fileName);

        FILE *file = CreateReceiveFile(fileName, "Received_files");
        if (file == NULL) {
            perror("Create file error");
            exit(EXIT_FAILURE);
        }

        struct fileInfo *info = new fileInfo;
        *info = {file, {0}};
        strcpy(info->fileName, fileName);

        filesMap[address] = info;


        bytesTransmitted = sendto(UdpServerDescr, &ACK, sizeof(ACK), 0,
                                  (struct sockaddr *) &(addr), rlen);
        if (bytesTransmitted < 0) {
            perror("send");
            exit(EXIT_FAILURE);
        }

    } else if (buf[0] == END && recvSize == 1) {

        struct fileInfo *info = pos->second;

        printf("File \"%s\" received\n", info->fileName);
        fclose(info->file);
        delete info;
        filesMap.erase(pos);

    } else {

        switch (fork()) {
        case -1:
            perror("fork()");
            exit(EXIT_FAILURE);

        case 0:                // child process
            {
                struct fileInfo *info = pos->second;
                if (fwrite(buf, 1, recvSize, info->file) < (size_t) recvSize) {
                    fprintf(stderr, "write file error\n");
                    exit(EXIT_FAILURE);
                }

                bytesTransmitted =
                    sendto(UdpServerDescr, &ACK, sizeof(ACK), 0,
                           (struct sockaddr *) &(addr), rlen);
                if (bytesTransmitted < 0) {
                    perror("send");
                    exit(EXIT_FAILURE);
                }
                exit(EXIT_SUCCESS);
            }

        default:
            break;
        }

    }
    return;
}
