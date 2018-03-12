#include <iostream>
#include <queue>
#include <functional>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <libgen.h>
#include <pthread.h>

using namespace std;

#define PORT_NUMBER_DATA 5277
#define PORT_NUMBER_ACK 4111
#define MAX_DATA 1024

typedef enum {
    REQ,
    ACK,
    FILE_REQ,
    FILE_ERR,
    FILE_REQ_ACK,
    SEND_FILE,
    DATA,
    TERM
}p_type;

typedef struct targs {
    int socket;
    int fd;
    struct sockaddr_in *addr;
}args;

typedef struct packet_t {
    p_type type;
    int seq_no;
    int length;
    uint8_t data[MAX_DATA];
} packet_t;

#define SEQ_OFFSET (sizeof(p_type))
#define LENGTH_OFFSET (sizeof(p_type) + sizeof(int))
#define DATA_OFFSET (sizeof(p_type) + sizeof(int) + sizeof(int))
#define META_SIZE (sizeof(p_type) + sizeof(int) + sizeof(int))
#define PACKET_SIZE (META_SIZE + MAX_DATA)

void encode(uint8_t *buffer, packet_t *packet) {
    memcpy(buffer, &(packet->type), sizeof(p_type));
    memcpy(buffer + SEQ_OFFSET, &(packet->seq_no), sizeof(int));
    memcpy(buffer + LENGTH_OFFSET, &(packet->length), sizeof(int));
    memcpy(buffer + DATA_OFFSET, &(packet->data), MAX_DATA);
}

void decode(uint8_t *buffer, packet_t *packet) {
    memcpy(&(packet->type), buffer, sizeof(p_type));
    memcpy(&(packet->seq_no), buffer + SEQ_OFFSET, sizeof(int));
    memcpy(&(packet->length), buffer + LENGTH_OFFSET, sizeof(int));
    memcpy(&(packet->data), buffer + DATA_OFFSET, MAX_DATA);
}

void encodeFilename(uint8_t *buffer, char *filename) {
    uint8_t len = (uint8_t) strlen(filename);
    len += 1;
    memcpy(buffer + LENGTH_OFFSET, &len, sizeof(uint8_t));
    memcpy(buffer + DATA_OFFSET, filename, strlen(filename));
}

void decodeFilename(uint8_t *buffer, char *filename) {
    uint8_t len = 0;
    memcpy(&len, buffer + LENGTH_OFFSET, sizeof(uint8_t));
    memcpy(filename, buffer + DATA_OFFSET, len);
}