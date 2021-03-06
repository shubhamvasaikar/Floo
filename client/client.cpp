/* UDP client in the internet domain */
#include "../packet.h"

void error(const char *msg) {
    perror(msg);
    exit(0);
}

bool Compare(packet_t a, packet_t b) {
    if (a.seq_no < b.seq_no)
        return false;
    else
        return true;
}

priority_queue<packet_t, vector<packet_t>, function<bool(packet_t, packet_t)>> fileQ(Compare);
queue<int> ack_queue;

pthread_mutex_t mutex;
pthread_cond_t emptyQ;

/*
 * The recieve thread is used to recieve data packets from the server.
 * It puts received packets in a priority queue called fileQ for ordered
 * file writing and sequence numbers in a shared data structure for sending
 * acknowledgements.
 */
void * recieve(void *args) {
    struct targs *tinfo = (struct targs *) args;
    int n = 0, flag = 1;
    int fWrite = tinfo->fd;
    int sockRecvr = tinfo->socket;
    struct sockaddr_in *serverSendr = tinfo->addr;
    int debug = 0;
    int prev_seq_no = 0;

    packet_t p;
    uint8_t  buffer[PACKET_SIZE];
    unsigned int length = sizeof(struct sockaddr_in);

    while (1){
        n = recvfrom(sockRecvr, buffer, PACKET_SIZE, 0, (struct sockaddr *)serverSendr, &length);
        decode(buffer, &p);

        //Uncommenting the next line will cause
        //the client to drop every 100th packet.
        //Use this to test retransmission.
        //if (p.seq_no % 3 == 0) (flag == 1) ? (flag = 0) : (flag = 1);

        //Critical section begins.
        pthread_mutex_lock(&mutex);
        if (flag) {
            ack_queue.push(p.seq_no);
            pthread_cond_signal(&emptyQ);
        }
        pthread_mutex_unlock(&mutex);
        //Critical sections ends.

        if (p.type == TERM)
            break;
                \
        if (debug == 1) printf("Got seq: %d\n",p.seq_no);
        //write(fWrite, p.data, p.length);

        if (flag) {
            fileQ.push(p);
        }

        if (p.seq_no == prev_seq_no + 1) {
            while (fileQ.size() > 0) {
                packet_t pckt = fileQ.top();
                if (pckt.seq_no == prev_seq_no + 1) {
                    write(fWrite, pckt.data, pckt.length);
                    fileQ.pop();
                    prev_seq_no = pckt.seq_no;
                }
            }
        }
        memset(buffer, 0, PACKET_SIZE);
        p.type = ACK;
        encode(buffer, &p);
        //if (flag)
        //n = sendto(sockSendr,buffer,PACKET_SIZE,0,(const struct sockaddr *)&serverRecvr,length);
        //prev_seq_no = p.seq_no;
    }
}

/*
 * This thread recieves a signal from the recieve thread after which it begins
 * execution and starts sending acknowledgements for the successfully recieved packets.
 * This is done by using the shared data structure ack_queue.
 */
void * transmit (void * args) {
    struct targs *tinfo = (struct targs *) args;
    int n = 0;
    int fWrite = tinfo->fd;
    int sockSendr = tinfo->socket;
    struct sockaddr_in *serverRecvr = tinfo->addr;
    int debug = 0;

    packet_t p;
    p.type = ACK;
    p.length = 0;
    uint8_t  buffer[PACKET_SIZE];
    int length = sizeof(struct sockaddr_in);

    while (1){

        //Critical section begins.
        pthread_mutex_lock(&mutex);
        if (ack_queue.empty()) {
            pthread_cond_wait(&emptyQ, &mutex);
        }
        p.seq_no = ack_queue.front();
        ack_queue.pop();
        pthread_mutex_unlock(&mutex);
        //Critical sections ends.

        if (p.seq_no == -1)
            break;

        encode(buffer, &p);
        n = sendto(sockSendr,buffer,PACKET_SIZE,0,(const struct sockaddr *)serverRecvr,length);
    }
}

/* The main method initiates the connection using the reciever and sender socket structures.
 * Once, the connection is established, and the file request is completed,
 * 2 threads are created:
 * recieve and transmit. These threads recieve data and transmit acknowledgements respectively.
 */
int main(int argc, char *argv[]) {
    int sockSendr, sockRecvr, n;
    unsigned int length;
    struct sockaddr_in serverSendr;       //serverSendr - port 4111
    struct sockaddr_in serverRecvr;       //serverRecvr - port 5277
    struct hostent *hp;              //hostent - hostname parameter
    uint8_t buffer[PACKET_SIZE];                //buffer is buffer
    packet_t p;
    args txinfo, rxinfo;
    pthread_t tx, rx;
    int debug = 0;

    mutex = PTHREAD_MUTEX_INITIALIZER;
    emptyQ = PTHREAD_COND_INITIALIZER;
    //Serializing...
    //bcopy(&p.seq_no, buffer);

    if (argc != 3) {

        printf("Usage: server port\n");
        exit(1);
    }

    sockSendr = socket(AF_INET, SOCK_DGRAM, 0);
    sockRecvr = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockSendr < 0 || sockRecvr < 0)
        error("socket");

    serverSendr.sin_family = AF_INET;
    serverRecvr.sin_family = AF_INET;
    hp = gethostbyname(argv[1]);      //returns hostent with the addr of server
    if (hp==0) error("Unknown host");

    memcpy((char *)&serverRecvr.sin_addr,
           (char *)hp->h_addr,         //Copies the server address from hp to server addr struct
           hp->h_length);
    serverRecvr.sin_port = htons(PORT_NUMBER_ACK);  //Converts the host byte order to network byte order

    length=sizeof(struct sockaddr_in);       //size of the address struct

    memset(buffer, 0, PACKET_SIZE);                       //Init buffer to zero

    //Connection Request.
    printf("Sending request.\n");
    p.type = REQ;
    p.seq_no = 0;
    memset(&(p.data), 0, MAX_DATA);
    encode(buffer, &p);
    n = sendto(sockSendr,buffer,PACKET_SIZE,0,(const struct sockaddr *)&serverRecvr,length);
    if (n < 0) error("Sendto");

    while (1) {
        n = recvfrom(sockSendr,buffer,PACKET_SIZE,0,(struct sockaddr *)&serverSendr, &length);
        if (n < 0) printf("Connection Failed.\n");
        decode (buffer, &p);
        if (p.type == ACK) break;
    }
    printf("Connection established.\n\n");

    //File request
    printf("Sending File name.\n");
    p.type = FILE_REQ;
    p.seq_no += 1;
    p.length = sizeof(argv[2]);
    encode(buffer, &p);
    encodeFilename(buffer, argv[2]);
    n = sendto(sockSendr,buffer,PACKET_SIZE,0,(const struct sockaddr *)&serverRecvr,length);
    if (n < 0) error("Sendto");

    while (1) {
        n = recvfrom(sockSendr,buffer,PACKET_SIZE,0,(struct sockaddr *)&serverSendr, &length);
        if (n < 0) printf("Connection Failed.\n");
        decode (buffer, &p);
        if (p.type == FILE_REQ_ACK) break;
        else error("File request error\n");
    }
    printf("File Request approved.\n\n");

    //File sending.
    int fWrite = open(basename(argv[2]), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fWrite < 0) error("File creation error.");

    int flag = 1;
    int prev_seq_no = 0;

    p.type = SEND_FILE;
    p.seq_no++;
    encode(buffer, &p);
    n = sendto(sockRecvr,buffer,PACKET_SIZE,0,(const struct sockaddr *)&serverRecvr,length);

    rxinfo.fd = fWrite;
    rxinfo.socket = sockRecvr;
    rxinfo.addr = &serverSendr;

    txinfo.fd = -1;
    txinfo.socket = sockSendr;
    txinfo.addr = &serverRecvr;

    pthread_create(&rx, NULL, &recieve, (void *) &rxinfo);
    pthread_create(&tx, NULL, &transmit, (void *) &txinfo);

    pthread_join(rx, NULL);
    pthread_join(tx, NULL);

//    int len_queue = ack_queue.size();
//    for (int i = 0; i < len_queue; i++) {
//        printf("ack_queue[%d]: %d\n", i, ack_queue.front());
//        ack_queue.pop();
//    }
//
//    len_queue = fileQ.size();
//    for (int i = 0; i < len_queue; i++) {
//        printf("pri_queue[%d]: %d\n", i, fileQ.top().seq_no);
//        fileQ.pop();
//    }

    close(sockSendr);
    return 0;
}
