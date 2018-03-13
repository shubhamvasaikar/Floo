/* Creates a datagram server.  The port
   number is passed as an argument.  This
   server runs forever */
#include "../packet.h"

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

/*
 * This structure is used to store timestamp and resend
 * status of a packet for storing it into the window.
 */
typedef struct wrapper_t {
    packet_t packet;
    bool resend;
    int timestamp;
}wrapper_t;

/*
 * This is the window for sending packets. When a packet is sent,
 * it is pushed onto the window. When an acknowledgement is recieved,
 * it is removed from the window.
 */
unordered_map<int, wrapper_t> window;

/*
 * This is the queue to implement the resending mechanism, if a timeout
 * has occured, packets, are marked for restransmission, and their
 * sequence_nos are pushed onto this queue.
 */
queue<int> resend_queue;
int window_size = 1;

pthread_mutex_t windowMutex;
pthread_cond_t windowFull;

pthread_mutex_t resend_mutex;

bool is_expired(int start, int stop) {
    double duration = 0;
    duration = (stop - start)/double(CLOCKS_PER_SEC)*1000;
    return duration > TIMEOUT ? true : false;
}

/*
 * This function will compare the timestamps of the packets in the window with
 * the threshold and mark packets for retransmission. In case of a timeout,
 * window size will be halved.
 */
void *timer_and_check(void * args) {
    int terminate = 0;
    while (1) {
        usleep(1000000);

        int current_time = clock();

        pthread_mutex_lock(&windowMutex);
        unordered_map<int, wrapper_t>::iterator it = window.begin();
        if (it != window.end()) {
            if ((window.size() == 1) && (it->second.packet.seq_no == -1)) {
                terminate = -1;
            }
        }
        while (it != window.end()) {
            if (is_expired(it->second.timestamp, current_time)) {
                resend_queue.push(it->second.packet.seq_no);
                it->second.resend = true;
                window_size /= 2;
            }
            it++;
        }
        pthread_mutex_unlock(&windowMutex);
        if (terminate == -1) break;
    }
}

/*
 * The transmit function is responsible for sending data packets
 * to the client. It also pushes packets to the window so that the reciever
 * can keep track of acknowledged packets.
 */
void *transmit(void *args) {
    struct targs *tinfo = (struct targs *) args;
    int n = 0, retries = 0;
    int bytesRead = 1;
    int fRead = tinfo->fd;
    int sockSendr = tinfo->socket;
    struct sockaddr_in *clientRecvr = tinfo->addr;
    std::unordered_map<int,wrapper_t>::iterator term_iter;

    wrapper_t w;
    packet_t p;
    uint8_t  buffer[PACKET_SIZE];
    socklen_t fromlen = sizeof(struct sockaddr_in);

    while (bytesRead > 0) {
        bytesRead = read(fRead, p.data, MAX_DATA);
        p.type = DATA;
        p.seq_no += 1;
        p.length = bytesRead;
        encode(buffer, &p);

        w.packet = p;
        w.resend = false;
        w.timestamp = clock();

        //Start of critical section.
        pthread_mutex_lock(&windowMutex);
        while (window.size() >= window_size) {
            pthread_cond_wait(&windowFull, &windowMutex);
        }
        window.insert({w.packet.seq_no, w});
        //pthread_cond_signal(&windowEmpty);
        pthread_mutex_unlock(&windowMutex);
        //End of critical section.

        retries += 1;
        n = sendto(sockSendr, buffer, PACKET_SIZE, 0, (struct sockaddr *) clientRecvr, fromlen);

        pthread_mutex_lock(&windowMutex);
        if (resend_queue.size() > 0) {
            while (resend_queue.size() > 0) {
                term_iter = window.find(resend_queue.front());
                p = term_iter->second.packet;
                n = sendto(sockSendr, buffer, PACKET_SIZE, 0, (struct sockaddr *) clientRecvr, fromlen);
                resend_queue.pop();
            }
        }
        pthread_mutex_unlock(&windowMutex);
    }
    p.type = TERM;
    p.seq_no = -1;
    p.length = 0;
    memset(buffer, 0, MAX_DATA);
    encode(buffer, &p);

    w.packet = p;
    w.resend = false;
    w.timestamp = clock();

    //Start of critical section.
    pthread_mutex_lock(&windowMutex);
    while (window.size() >= window_size) {
        pthread_cond_wait(&windowFull, &windowMutex);
    }
    window.insert({w.packet.seq_no, w});
    pthread_mutex_unlock(&windowMutex);
    //End of critical section.

    n = sendto(sockSendr, buffer, PACKET_SIZE, 0, (struct sockaddr *) clientRecvr, fromlen);
}

/*
 * The receive thread receives acknowledgements from the client and removes the packet
 * from the queue. It also increments the size of the window by 1 for every acknowledgement
 * recieved.
 */
void *recieve(void *args){
    struct targs *tinfo = (struct targs *) args;
    int n = 0, terminate = 0, size = 0;
    int bytesRead = 0;
    int fRead = tinfo->fd;
    int sockRecvr = tinfo->socket;
    struct sockaddr_in *clientSendr;
    std::unordered_map<int,wrapper_t>::const_iterator term_iter;

    wrapper_t w;
    packet_t p;
    uint8_t  buffer[PACKET_SIZE];
    socklen_t fromlen = sizeof(struct sockaddr_in);

    while(1) {

        n = recvfrom(sockRecvr, buffer, PACKET_SIZE, 0, (struct sockaddr *) clientSendr, &fromlen);
        decode(buffer, &p);

        //Enter the critical section.
        pthread_mutex_lock(&windowMutex);
        window.erase(p.seq_no);
        size = window.size();
        if (window_size < MAX_WINDOW_SIZE) {
            window_size += 1;
        }
        term_iter = window.find(-1);
        if (term_iter != window.end()) {
            if ((size == 1) && (term_iter->second.packet.seq_no == -1)){
                terminate = -1;
            }
        }
        pthread_cond_signal(&windowFull);
        pthread_mutex_unlock(&windowMutex);
        //Exit the critical section.

        if (terminate == -1) {
            break;
        }
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
    pthread_t tx, rx, cx;
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

    retries = 0;

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
        return 0;
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
    pthread_create(&rx, NULL, &recieve, (void *) &rxinfo);
    pthread_create(&cx, NULL, &timer_and_check, NULL);

    pthread_join(cx, NULL);
    pthread_join(tx, NULL);
    pthread_join(rx, NULL);

    return 0;
 }
