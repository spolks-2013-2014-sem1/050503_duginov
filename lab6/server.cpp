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

#include <iostream>
#include <map>
#include <cstdint>

#define replyBufSize 256
#define bufSize 4096

using namespace std;

const unsigned char ACK = 1;

struct fileInfo {
    FILE *file;
    char fileName[256];
    long totalBytesReceived;
    long fileSize;
};


void receiveFileTCP(char *serverName, unsigned int port);
void receiveFileUDP(char *serverName, unsigned int port);
int TCP_Processing(int descr, map < int, fileInfo * >&filesMap);
void TCP_OOB_Processing(int descr, map < int, fileInfo * >&filesMap);
void UDP_Processing(int UdpServerDescr, map <uint64_t, fileInfo*> &filesMap);


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
        cerr << "usage: main <host> <port> [-u]\n";
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

//------------------------------------------------//
//------------------TCP SERVER--------------------//
//------------------------------------------------//


void receiveFileTCP(char *hostName, unsigned int port)
{
    struct sockaddr_in sin;

    if ((TcpServerDescr =
         createTcpServerSocket(hostName, port, &sin, 5)) == -1) {
        cerr << "Creation socket error\n";
        exit(EXIT_FAILURE);
    }

    fd_set rfds, afds, xset;
    FD_ZERO(&afds);
    FD_ZERO(&xset);
    FD_SET(TcpServerDescr, &afds);

    int nfds = getdtablesize();
    int rsd;

    socklen_t rlen;
    struct sockaddr_in remote;

    map < int, fileInfo * >filesMap;

    while (1) {
        memcpy(&rfds, &afds, sizeof(rfds));

        if (select(nfds, &rfds, NULL, &xset, NULL) < 0) {
            perror("Select");
            return;
        }
        if (FD_ISSET(TcpServerDescr, &rfds)) {
            rlen = sizeof(remote);
            rsd =
                accept(TcpServerDescr, (struct sockaddr *) &remote, &rlen);
            FD_SET(rsd, &afds);
            FD_SET(rsd, &xset);
        }
        for (rsd = 0; rsd < nfds; ++rsd) {
            // search descriptors with exceptions (e.g. out-of-band data)
            if ((rsd != TcpServerDescr) && FD_ISSET(rsd, &xset)) {
                TCP_OOB_Processing(rsd, filesMap);
                FD_CLR(rsd, &xset);
            }
            // search descriptors ready to read
            if ((rsd != TcpServerDescr) && FD_ISSET(rsd, &rfds)) {
                if (TCP_Processing(rsd, filesMap) == 0) {
                    close(rsd);
                    FD_CLR(rsd, &afds);
                } else
                    FD_SET(rsd, &xset);
            }
        }
    }
}

int TCP_Processing(int descr, map < int, fileInfo * >&filesMap)
{
    char buf[bufSize], requestBuf[replyBufSize];
    int recvSize;

    map < int, fileInfo * >::iterator pos = filesMap.find(descr);

    // descr not found in array
    if (pos == filesMap.end()) {
        recvSize =
            recv(descr, requestBuf, sizeof(requestBuf), MSG_WAITALL);
        if (recvSize == -1) {
            perror("recv");
            exit(EXIT_FAILURE);
        }

        char *fileName = strtok(requestBuf, ":");
        if (fileName == NULL) {
            cerr << "Bad file name\n";
            exit(EXIT_FAILURE);
        }
        char *size = strtok(NULL, ":");
        if (size == NULL) {
            cerr << "Bad file size\n";
            exit(EXIT_FAILURE);
        }
        long fileSize = atoi(size);
        cout << "File size: " << fileSize << ", file name: "
            << fileName << "\n";

        FILE *file = CreateReceiveFile(fileName, "Received_files");
        if (file == NULL) {
            perror("Create file error");
            exit(EXIT_FAILURE);
        }
        struct fileInfo *info = new fileInfo;
        *info = {file, {0}, 0, fileSize};
        strcpy(info->fileName, fileName);

        filesMap[descr] = info;

    } else {
        struct fileInfo *info = pos->second;

        recvSize = recv(descr, buf, sizeof(buf), MSG_DONTWAIT);
        if (recvSize < 0) {
            perror("recv");
            exit(EXIT_FAILURE);
        } else if (recvSize == 0) {
            cout << "File \"" << info->fileName << "\" received\n";
            fclose(info->file);
            delete info;
            filesMap.erase(pos);
            return 0;
        } else {
            info->totalBytesReceived += recvSize;
            fwrite(buf, 1, recvSize, info->file);
        }
    }
    return recvSize;
}


void TCP_OOB_Processing(int descr, map < int, fileInfo * >&filesMap)
{
    map < int, fileInfo * >::iterator pos = filesMap.find(descr);
    if (pos == filesMap.end())  // descr not found in array
        return;

    else {
        char oobBuf;
        struct fileInfo *info = pos->second;

        int recvSize = recv(descr, &oobBuf, sizeof(oobBuf), MSG_OOB);
        if (recvSize == -1)
            cerr << "recv OOB error\n";
        else
            cout << "OOB byte received. Total received bytes of \""
                << info->fileName << "\": "
                << info->totalBytesReceived << "\n";
    }
    return;
}



//------------------------------------------------//
//------------------UDP SERVER--------------------//
//------------------------------------------------//

void receiveFileUDP(char *hostName, unsigned int port)
{
    struct sockaddr_in sin;
    int UdpServerDescr;

    if ((UdpServerDescr =
         createUdpServerSocket(hostName, port, &sin)) == -1) {
        cerr << "Creation socket error\n";
        exit(EXIT_FAILURE);
    }

    struct timeval timeOut = { 30, 0 };
    struct timeval *timePTR;

    fd_set rfds;
    int nfds = getdtablesize();
    FD_ZERO(&rfds);
    map < uint64_t, fileInfo * >filesMap;

    while (1) {

        if (filesMap.size() == 0)
            timePTR = NULL;     // waiting for new clients infinitely
        else {
            timeOut = {30, 0};
            timePTR = &timeOut;
        }

        FD_SET(UdpServerDescr, &rfds);
        if (select(nfds, &rfds, (fd_set *) 0, (fd_set *) 0, timePTR) < 0) {
            perror("Select");
            return;
        }
        if (timePTR != NULL && timePTR->tv_sec == 0) {
            cerr << "Timeout error\n";
            return;
        }
        if (FD_ISSET(UdpServerDescr, &rfds)) {

            UDP_Processing(UdpServerDescr, filesMap);
        }
    }
}


void UDP_Processing(int UdpServerDescr, map <uint64_t, fileInfo*> &filesMap)
{
    char buf[bufSize];
    int recvSize;

    struct sockaddr_in addr;
    socklen_t rlen = sizeof(addr);

    recvSize =
        recvfrom(UdpServerDescr, buf, sizeof(buf), MSG_DONTWAIT,
                 (struct sockaddr *) &addr, &rlen);
    if (recvSize == -1) {
        perror("recv");
        exit(EXIT_FAILURE);
    }

    int bytesTransmitted = sendto(UdpServerDescr, &ACK, sizeof(ACK), 0,
                                  (struct sockaddr *) &addr, rlen);
    if (bytesTransmitted < 0) {
        perror("send");
        exit(EXIT_FAILURE);
    }

    uint64_t address = IpPortToNumber(addr.sin_addr.s_addr, addr.sin_port);

    map < uint64_t, fileInfo * >::iterator pos = filesMap.find(address);

    // client address not found in array
    if (pos == filesMap.end()) {
        char *fileName = strtok(buf, ":");
        if (fileName == NULL) {
            cerr << "Bad file name\n";
            exit(EXIT_FAILURE);
        }
        char *size = strtok(NULL, ":");
        if (size == NULL) {
            cerr << "Bad file size\n";
            exit(EXIT_FAILURE);
        }
        long fileSize = atoi(size);
        cout << "File size: " << fileSize << ", file name: "
            << fileName << "\n";

        FILE *file = CreateReceiveFile(fileName, "Received_files");
        if (file == NULL) {
            perror("Create file error");
            exit(EXIT_FAILURE);
        }

        struct fileInfo *info = new fileInfo;
        *info = {file, {0}, 0, fileSize};
        strcpy(info->fileName, fileName);

        filesMap[address] = info;

    } else {
        struct fileInfo *info = pos->second;
        info->totalBytesReceived += recvSize;

        fwrite(buf, 1, recvSize, info->file);

        if (info->totalBytesReceived == info->fileSize) {
            cout << "File \"" << info->fileName << "\" received\n";
            fclose(info->file);
            delete info;
            filesMap.erase(pos);
        }
    }
    return;
}
