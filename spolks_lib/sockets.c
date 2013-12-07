#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>

#define h_addr h_addr_list[0]

// Create TCP socket and fill in sockaddr_in structure
int createTcpSocket(char *hostName, unsigned short port,
                    struct sockaddr_in *sin)
{
    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;

    struct hostent *hptr = gethostbyname(hostName);
    if (hptr != NULL)
        memcpy(&sin->sin_addr, hptr->h_addr, hptr->h_length);   // sin_addr contain ip which bind to socket
    else {
        perror("Incorrect host name");
        return -1;
    }

    sin->sin_port = htons(port);        // convert to network byte order

    struct protoent *pptr = getprotobyname("TCP");
    if (pptr == NULL) {
        fprintf(stderr, "Incorrect protocol name\n");
        return -1;
    }
    // Create socket
    int socketDescriptor = socket(PF_INET, SOCK_STREAM, pptr->p_proto);
    if (socketDescriptor < 0) {
        perror("Create socket error");
        return -1;
    }

    return socketDescriptor;
}


int createTcpServerSocket(char *hostName, unsigned short port,
                          struct sockaddr_in *sin, int qlen)
{
    int socketDescriptor;

    // Create socket
    if ((socketDescriptor =
         createTcpSocket(hostName, port,
                         (struct sockaddr_in *) sin)) == -1) {
        return -1;
    }
    // allow server to bind to an address which is in a TIME_WAIT state
    int opt = 1;
    if (setsockopt
        (socketDescriptor, SOL_SOCKET, SO_REUSEADDR, &opt,
         sizeof(int)) == -1) {
        perror("setsockopt");
        return -1;
    }
    // Bind socket
    if (bind(socketDescriptor, (struct sockaddr *) sin, sizeof(*sin)) < 0) {
        perror("Bind socket error");
        return -1;
    }
    // Switch socket into passive mode 
    if (listen(socketDescriptor, qlen) == -1) {
        perror("Socket passive mode error");
        return -1;
    }

    return socketDescriptor;
}


// Create UDP socket and fill in sockaddr_in structure
int createUdpSocket(char *hostName, unsigned short port,
                    struct sockaddr_in *sin)
{
    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;

    struct hostent *hptr = gethostbyname(hostName);
    if (hptr != NULL)
        memcpy(&sin->sin_addr, hptr->h_addr, hptr->h_length);   // sin_addr contain ip which bind to socket
    else {
        perror("Incorrect host name");
        return -1;
    }

    sin->sin_port = htons(port);        // convert to network byte order

    struct protoent *pptr = getprotobyname("UDP");
    if (pptr == NULL) {
        fprintf(stderr, "Incorrect protocol name\n");
        return -1;
    }
    // Create socket
    int socketDescriptor = socket(PF_INET, SOCK_DGRAM, pptr->p_proto);
    if (socketDescriptor < 0) {
        perror("Create socket error");
        return -1;
    }

    return socketDescriptor;
}


int createUdpServerSocket(char *hostName, unsigned short port,
                          struct sockaddr_in *sin)
{
    int socketDescriptor;

    // Create socket
    if ((socketDescriptor =
         createUdpSocket(hostName, port,
                         (struct sockaddr_in *) sin)) == -1) {
        return -1;
    }
    // Bind socket
    if (bind(socketDescriptor, (struct sockaddr *) sin, sizeof(*sin)) < 0) {
        perror("Bind socket error");
        return -1;
    }

    return socketDescriptor;
}

// Receive data from socket to buffer
int ReceiveToBuf(int descriptor, char *buf, int len)
{
    int recvSize = 0;
    int numberOfBytesRead;
    while (recvSize < len) {
        numberOfBytesRead =
            recv(descriptor, buf + recvSize, len - recvSize, 0);
        if (numberOfBytesRead == 0)
            break;
        else if (numberOfBytesRead < 0)
            return -1;
        else
            recvSize += numberOfBytesRead;
    }
    return recvSize;
}
