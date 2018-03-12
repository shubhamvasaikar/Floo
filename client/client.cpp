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

priority_queue<packet_t, vector<packet_t>, function<bool(packet_t, packet_t)>> pq(Compare);
queue<int> ack_queue;
//priority_queue<int, vector<int>, function<bool(int, int)>> pq(Compare);


void * recieve(void *args) {
    struct targs *tinfo = (struct targs *) args;
    int n = 0;
    int fWrite = tinfo->fd;
    int sockRecvr = tinfo->socket;
    struct sockaddr_in *serverSendr = tinfo->addr;
    int debug = 0;

    packet_t p;
    uint8_t  buffer[PACKET_SIZE];
    unsigned int length = sizeof(struct sockaddr_in);

    while (1){
        n = recvfrom(sockRecvr, buffer, PACKET_SIZE, 0, (struct sockaddr *)serverSendr, &length);
        decode(buffer, &p);
        if (p.type == TERM)
            break;
        ack_queue.push(p.seq_no);
        pq.push(p);
        if (debug == 1) printf("Got seq: %d\n",p.seq_no);
        write(fWrite, p.data, p.length);

        //Uncommenting the next line will cause
        //the client to drop every 100th packet.
        //Use this to test retransmission.
        //if (p.seq_no % 100 == 0) (flag == 1) ? (flag = 0) : (flag = 1);

/*        if (p.seq_no != prev_seq_no) write(fWrite, p.data, p.length);
        memset(buffer, 0, PACKET_SIZE);
        p.type = ACK;
        encode(buffer, &p);
        if (flag)
            n = sendto(sockSendr,buffer,PACKET_SIZE,0,(const struct sockaddr *)&serverRecvr,length);
        prev_seq_no = p.seq_no; */
    }
}

void * transmit (void * args) {
    struct targs *tinfo = (struct targs *) args;
    int n = 0;
    int fWrite = tinfo->fd;
    int sockSendr = tinfo->socket;
    struct sockaddr_in *serverRecvr = tinfo->addr;
    int debug = 0;

    packet_t p;
    uint8_t  buffer[PACKET_SIZE];
    int length = sizeof(struct sockaddr_in);

    while (1){
        n = sendto(sockSendr,buffer,PACKET_SIZE,0,(const struct sockaddr *)&serverRecvr,length);
    }
}

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

    pthread_create(&rx, NULL, &recieve, (void *) &rxinfo);

    pthread_join(rx, NULL);

    int len_queue = ack_queue.size();
    for (int i = 0; i < len_queue; i++) {
        printf("ack_queue[%d]: %d\n", i, ack_queue.front());
        ack_queue.pop();
    }

    len_queue = pq.size();
    for (int i = 0; i < len_queue; i++) {
        printf("pri_queue[%d]: %d\n", i, pq.top().seq_no);
        pq.pop();
    }

    close(sockSendr);
    return 0;
}
