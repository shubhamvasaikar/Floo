/* Creates a datagram server.  The port
   number is passed as an argument.  This
   server runs forever */
#include "../packet.h"

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

void *transmit(void *args) {
    struct targs *tinfo = (struct targs *) args;
    int n = 0, retries = 0;
    int bytesRead = 1;
    int fRead = tinfo->fd;
    int sockSendr = tinfo->socket;
    struct sockaddr_in *clientRecvr = tinfo->addr;

    packet_t p;
    uint8_t  buffer[PACKET_SIZE];
    socklen_t fromlen = sizeof(struct sockaddr_in);

    while (bytesRead > 0) {
        bytesRead = read(fRead, p.data, MAX_DATA);
        p.type = DATA;
        p.seq_no += 1;
        p.length = bytesRead;
        encode(buffer, &p);

        retries += 1;
        n = sendto(sockSendr, buffer, PACKET_SIZE, 0, (struct sockaddr *) clientRecvr, fromlen);
        if (n < 0) continue;
        decode(buffer, &p);
    }
    p.type = TERM;
    p.seq_no += 1;
    p.length = 0;
    memset(buffer, 0, MAX_DATA);
    encode(buffer, &p);
    n = sendto(sockSendr, buffer, PACKET_SIZE, 0, (struct sockaddr *) clientRecvr, fromlen);
    printf("No. of retries: %d\n\n", (retries - (p.seq_no - 4)));
}

void *recieve(void *args){
    struct targs *tinfo = (struct targs *) args;
    int n = 0, retries = 0;
    int bytesRead = 0;
    int fRead = tinfo->fd;
    int sockRecvr = tinfo->socket;
    struct sockaddr_in *clientSendr;
    packet_t p;
    uint8_t  buffer[PACKET_SIZE];
    socklen_t fromlen = sizeof(struct sockaddr_in);

    while(1) {
        n = recvfrom(sockRecvr,buffer,PACKET_SIZE,0,(struct sockaddr *)clientSendr, &fromlen);
    }
}

int main(int argc, char *argv[])
{
    int sockSendr, sockRecvr, length, n;             //sock - return value of sock
    socklen_t fromlen;               //fromlen  - size of the client address
    struct sockaddr_in serverSendr;       //serverSendr - port 4111
    struct sockaddr_in serverRecvr;       //serverRecvr - port 5277
    struct sockaddr_in clientSendr;
    struct sockaddr_in clientRecvr;
    pthread_t tx, rx;
    args txinfo, rxinfo;
    uint8_t buffer[PACKET_SIZE];                 // buf is buffer
    int bytesRead = 0;
    char filename[256];
    int retries = 0;

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    packet_t p;
    p.length = 0;

    if (argc < 1) {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(0);
    } //port number

    sockSendr = socket(AF_INET, SOCK_DGRAM, 0);     //AF_INET - address family
    sockRecvr = socket(AF_INET, SOCK_DGRAM, 0);     //AF_INET - address family
    //socket() - creates a socket  int sock - holds the file descriptor for the socket

    if (sockSendr < 0 || sockRecvr < 0) error("Opening socket");

    length = sizeof(struct sockaddr_in);           //length - size of server address struct
    memset(&serverRecvr, 0, length);             //initialize the server struct to zero
    serverRecvr.sin_family = AF_INET;         //
    serverRecvr.sin_addr.s_addr = INADDR_ANY; //For server, it holds the IP of the machine it is running on

    memset(&serverSendr, 0, length);             //initialize the server struct to zero
    serverSendr.sin_family = AF_INET;         //
    serverSendr.sin_addr.s_addr = INADDR_ANY; //For server, it holds the IP of the machine it is running on

    //htons() - converts the portno from host byte order to network byte order
    serverRecvr.sin_port=htons(PORT_NUMBER_ACK); //atoi - charArray to int

    serverSendr.sin_port=htons(PORT_NUMBER_DATA); //atoi - charArray to int

    //binding the server address to the socket file desc
    if ((bind(sockSendr,(struct sockaddr *)&serverSendr,length) < 0) ||
        (bind(sockRecvr,(struct sockaddr *)&serverRecvr,length) < 0))
        error("binding");

    //initializing the size for the client address struct
    fromlen = sizeof(struct sockaddr_in);

    while(1) {
        retries = 0;
        timeout.tv_usec = 0;
        setsockopt(sockRecvr,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));

        //Listen and accept for connections.
        printf("Listening for connection.\n");
        //recvFrom(sockfd, *buffer, size_t length of buff, flags, source address struct, address length) 'n' will have message/packet-length
        while (1) {
            n = recvfrom(sockRecvr, buffer, PACKET_SIZE, 0, (struct sockaddr *)&clientSendr, &fromlen);
            if (n < 0) printf("Connection failed.");
            decode (buffer, &p);
            if (p.type == REQ) break;
        }
        printf("Accepting Connection.\n\n");
        p.type = ACK;
        p.seq_no += 1;
        encode(buffer, &p);
        n = sendto(sockSendr, buffer, PACKET_SIZE, 0, (struct sockaddr *)&clientSendr, fromlen);

        //Accept File request.
        printf("Waiting for file request.\n");
        while (1) {
            n = recvfrom(sockRecvr, buffer, PACKET_SIZE, 0, (struct sockaddr *)&clientSendr, &fromlen);
            if (n < 0) printf("Connection failed.");
            decode (buffer, &p);
            if (p.type == FILE_REQ) break;
        }
        decodeFilename(buffer, filename);
        printf("Filename: %s\n", filename);
        if (access(filename, F_OK) != -1) {
            p.type = FILE_REQ_ACK;
            printf("File request accepted.\n\n");
            p.seq_no += 1;
            p.length = 0;
            encode(buffer, &p);
            n = sendto(sockSendr, buffer, PACKET_SIZE, 0, (struct sockaddr *)&clientSendr, fromlen);
        }
        else {
            p.type = FILE_ERR;
            p.seq_no += 1;
            p.length = 0;
            encode(buffer, &p);
            n = sendto(sockSendr, buffer, PACKET_SIZE, 0, (struct sockaddr *)&clientSendr, fromlen);
            printf("File error.\n\n");
            continue;
        }

        //File send.
        while(1) {
            //printf("Before recvfrom()");
            n = recvfrom(sockRecvr, buffer, PACKET_SIZE, 0, (struct sockaddr *)&clientRecvr, &fromlen);
            decode(buffer, &p);
            if(p.type == SEND_FILE) break;
        }

        int fRead = open(filename, 'r');
        if (fRead < 0) error("File not found.");

        timeout.tv_usec = 100000; //100ms timeout before retransmission.
        //setsockopt(sockRecvr,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));

        txinfo.fd = fRead;
        txinfo.socket = sockSendr;
        txinfo.addr = &clientRecvr;

        rxinfo.fd = 0;
        rxinfo.socket = sockRecvr;
        rxinfo.addr = &clientSendr;

        pthread_create(&tx, NULL, &transmit, (void *) &txinfo);
        //pthread_create(&rx, NULL, &recieve, (void *) &rxinfo);

        pthread_join(tx, NULL);
        //pthread_join(rx, NULL);
    }
    return 0;
 }
