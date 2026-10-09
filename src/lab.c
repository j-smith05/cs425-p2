
#include "lab.h"
#include <string.h>

// Calculate the Internet checksum
uint16_t checksum(const uint8_t *data, size_t length)
{
    uint32_t sum = 0;

    for (size_t i = 0; i < length; i += 2)
    {
        uint16_t word = (uint16_t)data[i] << 8;

        if (i + 1 < length)
            word |= data[i + 1];

        sum += word;
    }

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)~sum;
}

// Convert a packet into bytes for sending
int encode_packet(const Packet *packet, uint8_t *buffer)
{
    if (!packet || !buffer || packet->type > FIN ||
        packet->length > MAX_PAYLOAD)
        return -1;

    if (packet->type != DATA && packet->length != 0)
        return -1;

    int size = 10 + packet->length;
    memset(buffer, 0, size);

    buffer[0] = packet->type;

    // Store sequence number in network byte order
    buffer[4] = (uint8_t)(packet->seq >> 24);
    buffer[5] = (uint8_t)(packet->seq >> 16);
    buffer[6] = (uint8_t)(packet->seq >> 8);
    buffer[7] = (uint8_t)packet->seq;

    buffer[8] = (uint8_t)(packet->length >> 8);
    buffer[9] = (uint8_t)packet->length;

    memcpy(buffer + 10, packet->payload, packet->length);

    uint16_t check = checksum(buffer, size);
    buffer[2] = (uint8_t)(check >> 8);
    buffer[3] = (uint8_t)check;

    return size;
}

// Decode and validate a received packet
int decode_packet(const uint8_t *buffer, size_t size, Packet *packet)
{
    if (!buffer || !packet || size < 10 || size > MAX_PACKET)
        return -1;

    uint16_t length = ((uint16_t)buffer[8] << 8) | buffer[9];

    if (buffer[0] > FIN || buffer[1] != 0 ||
        length > MAX_PAYLOAD || size != 10 + length)
        return -1;

    if (buffer[0] != DATA && length != 0)
        return -1;

    // A valid packet has a checksum result of zero
    if (checksum(buffer, size) != 0)
        return -1;

    packet->type = buffer[0];
    packet->seq = ((uint32_t)buffer[4] << 24) |
                  ((uint32_t)buffer[5] << 16) |
                  ((uint32_t)buffer[6] << 8) |
                  buffer[7];

    packet->length = length;
    memcpy(packet->payload, buffer + 10, length);

    return 0;
}

// Set up the sender
void sender_init(Sender *sender, int window, int timeout)
{
    sender->base = 0;
    sender->next = 0;
    sender->window = window;
    sender->timeout = timeout;
    sender->retries = 0;
    sender->timer_start = -1;
}

// Check whether the sender has room in its window
int sender_can_send(const Sender *sender)
{
    return sender->next < sender->base + sender->window;
}

// Process cumulative acknowledgments
void sender_ack(Sender *sender, uint32_t ack, int64_t now)
{
    if (ack > sender->base && ack <= sender->next)
    {
        sender->base = ack;
        sender->retries = 0;

        // Restart timer if packets are still waiting
        if (sender->base < sender->next)
            sender->timer_start = now;
        else
            sender->timer_start = -1;
    }
}

// Check whether the sender needs to retransmit
int sender_timeout(Sender *sender, int64_t now)
{
    if (sender->timer_start < 0 ||
        sender->base == sender->next)
        return 0;

    if (now - sender->timer_start < sender->timeout)
        return 0;

    sender->retries++;
    sender->timer_start = now;

    if (sender->retries >= 10)
        return -1; // Give up after 10 timeouts

    return 1; // Resend the unacknowledged window
}

// Set up the receiver
void receiver_init(Receiver *receiver)
{
    receiver->expected = 0;
    receiver->finished = 0;
}

// Process an incoming packet
int receiver_packet(Receiver *receiver, const Packet *packet)
{
    if (receiver->finished)
        return 0;

    if (packet->seq != receiver->expected)
        return 0; // Duplicate or out-of-order packet

    if (packet->type == DATA)
    {
        receiver->expected++;
        return 1; // Deliver payload and send ACK
    }

    if (packet->type == FIN)
    {
        receiver->expected++;
        receiver->finished = 1;
        return 2; // Send final ACK and begin linger
    }

    return 0;
}
