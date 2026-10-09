
#ifndef LAB_H
#define LAB_H

#include <stddef.h>
#include <stdint.h>

#define MAX_PAYLOAD 1024
#define MAX_PACKET 1034
#define MAX_WINDOW 64

#define DATA 0
#define ACK  1
#define FIN  2

// Packet structure
typedef struct {
    uint8_t type;
    uint32_t seq;
    uint16_t length;
    uint8_t payload[MAX_PAYLOAD];
} Packet;

// Sender state
typedef struct {
    uint32_t base;
    uint32_t next;
    int window;
    int timeout;
    int retries;
    int64_t timer_start;
} Sender;

// Receiver state
typedef struct {
    uint32_t expected;
    int finished;
} Receiver;

// Packet functions
uint16_t checksum(const uint8_t *data, size_t length);
int encode_packet(const Packet *packet, uint8_t *buffer);
int decode_packet(const uint8_t *buffer, size_t size, Packet *packet);

// Sender functions
void sender_init(Sender *sender, int window, int timeout);
int sender_can_send(const Sender *sender);
void sender_ack(Sender *sender, uint32_t ack, int64_t now);
int sender_timeout(Sender *sender, int64_t now);

// Receiver functions
void receiver_init(Receiver *receiver);
int receiver_packet(Receiver *receiver, const Packet *packet);

#endif // LAB_H
