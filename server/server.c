/* Creates a datagram server.  The port 
   number is passed as an argument.  This
   server runs forever */
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <stdio.h>
#include "../packet.h"

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

int main(int argc, char *argv[])
{
    int sock, length, n;             //sock - return value of sock
    socklen_t fromlen;               //fromlen  - size of the client address
    struct sockaddr_in server;       //server - server address structure
    struct sockaddr_in from;         //from - client  address structure
    uint8_t buffer[PACKET_SIZE];                 // buf is buffer
    int bytesRead = 0;
    char filename[256];
    int retries = 0;

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    packet_t p;
    p.length = 0;

    if (argc < 2) {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(0);
    } //port number
   
    sock = socket(AF_INET, SOCK_DGRAM, 0);     //AF_INET - address family  
    //socket() - creates a socket  int sock - holds the file descriptor for the socket
   
    if (sock < 0) error("Opening socket");      
   
    length = sizeof(server);           //length - size of server address struct
    memset(&server, 0, length);             //initialize the server struct to zero
    server.sin_family=AF_INET;         //
    server.sin_addr.s_addr=INADDR_ANY; //For server, it holds the IP of the machine it is running on
  
    //htons() - converts the portno from host byte order to network byte order
    server.sin_port=htons(atoi(argv[1])); //atoi - charArray to int 
   
    //binding the server address to the socket file desc
    if (bind(sock,(struct sockaddr *)&server,length) < 0)      
        error("binding");
   
    //initializing the size for the client address struct
    fromlen = sizeof(struct sockaddr_in);
    
    while(1) {
        retries = 0;
        timeout.tv_usec = 0;
        setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));

        //Listen and accept for connections.
        printf("Listening for connection.\n");
        //recvFrom(sockfd, *buffer, size_t length of buff, flags, source address struct, address length) 'n' will have message/packet-length
        while (1) {
            n = recvfrom(sock, buffer, PACKET_SIZE, 0, (struct sockaddr *)&from, &fromlen);
            if (n < 0) printf("Connection failed.");
            decode (buffer, &p);
            if (p.type == REQ) break;
        }
        printf("Accepting Connection.\n\n");
        p.type = ACK;
        p.seq_no += 1;
        encode(buffer, &p);
        n = sendto(sock, buffer, PACKET_SIZE, 0, (struct sockaddr *)&from, fromlen);

        //Accept File request.
        printf("Waiting for file request.\n");
        while (1) {
            n = recvfrom(sock, buffer, PACKET_SIZE, 0, (struct sockaddr *)&from, &fromlen);
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
            n = sendto(sock, buffer, PACKET_SIZE, 0, (struct sockaddr *)&from, fromlen);
        }
        else {
            p.type = FILE_ERR;
            p.seq_no += 1;
            p.length = 0;
            encode(buffer, &p);
            n = sendto(sock, buffer, PACKET_SIZE, 0, (struct sockaddr *)&from, fromlen);
            printf("File error.\n\n");
            continue;
        }
        
        //File send.    
        int fRead = open(filename, 'r');
        if (fRead < 0) error("File not found.");

        timeout.tv_usec = 100000; //100ms timeout before retransmission.
        setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));

        while(1) {
            bytesRead = read(fRead, p.data, MAX_DATA);
            if (bytesRead < 1){
                p.type = TERM;
                p.seq_no += 1;
                p.length = 0;
                memset(buffer, 0, MAX_DATA);
                encode(buffer, &p);
                n = sendto(sock,buffer,PACKET_SIZE,0,(struct sockaddr *)&from,fromlen);
                printf("No. of retries: %d\n\n", (retries - (p.seq_no - 4))); 
                break;
            }
            p.type = DATA;
            p.seq_no += 1;
            p.length = bytesRead;
            encode(buffer, &p);
            //n = sendto(sock,buffer,PACKET_SIZE,0,(struct sockaddr *)&from,fromlen);

            while (1) {
                retries += 1;
                n = sendto(sock,buffer,PACKET_SIZE,0,(struct sockaddr *)&from,fromlen);
                n = recvfrom(sock,buffer,PACKET_SIZE,0,(struct sockaddr *)&from, &length);
                if (n < 0) continue;
                decode(buffer, &p);
                if (p.type == ACK) break;
            }
        }
    }
    return 0;
 }
